import csv
import json

import pytest

import replay_balance_report as report

HEADER = 'schema,kind,sequence,closing_swap,api,path,shards,scheduling,commands,draws,wall_ns,prepare_ns,parallel_ns,join_wait_ns,direct_decode_ns,direct_arena_ns,dispatch_ns,shard,begin_ns,end_ns,arena_ns,decode_ns,clock_anchor_ns,clock_anchor_utc_ns'.split(',')


def batch_rows(sequence, swap, wall_ns, dispatch_ns, slow_shard=0):
    base = dict.fromkeys(HEADER, '')
    base.update(schema=1, sequence=sequence, closing_swap=swap, api='vulkan', path='prepared',
                shards=2, scheduling='contiguous', clock_anchor_ns=100_000_000,
                clock_anchor_utc_ns=1_000_000_000)
    batch = dict(base, kind='batch', commands=4, draws=2, wall_ns=wall_ns,
                 prepare_ns=100_000, parallel_ns=wall_ns-100_000, join_wait_ns=0,
                 dispatch_ns=dispatch_ns, shard=-1)
    rows = [batch]
    maximum = wall_ns//2+500_000
    for shard in range(2):
        duration = maximum if shard == slow_shard else maximum//2
        rows.append(dict(base, kind='shard', shard=shard, commands=2, draws=1,
                         begin_ns=dispatch_ns+1000, end_ns=dispatch_ns+1000+duration,
                         arena_ns=1000, decode_ns=duration-1000))
    return rows


def write_trace(path, rows):
    with path.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=HEADER)
        writer.writeheader()
        writer.writerows(rows)
    return path


def trace(tmp_path):
    rows = (batch_rows(1, 10, 2_000_000, 100_000_000)
            + batch_rows(2, 0, 2_000_000, 200_000_000)
            + batch_rows(3, 11, 3_000_000, 230_000_000, 1)
            + batch_rows(4, 0, 2_000_000, 260_000_000))
    return write_trace(tmp_path/'replay.csv', rows), rows


def test_percentiles_and_empty_population():
    summary = report.summarize(list(range(1, 101)))
    assert summary['samples'] == 100
    assert [summary[x] for x in ('p50', 'p95', 'p99')] == [50, 95, 99]
    assert report.summarize([])['p95'] is None


def test_complete_frames_and_two_distinct_shard_maxima(tmp_path):
    path, _ = trace(tmp_path)
    result = report.aggregate_frames(report.load_replay(path))
    assert result['dropped_prefix_batches'] == 1
    assert result['dropped_suffix_batches'] == 1
    assert len(result['frames']) == 1
    frame = result['frames'][0]
    assert frame['swap'] == 11
    assert frame['replay_ns'] == 5_000_000
    assert frame['sum_batch_max_shard_ns'] == 3_500_000
    assert frame['max_shard_total_ns'] == 2_750_000


@pytest.mark.parametrize('mutation', ['schema', 'duplicate_shard', 'missing_shard', 'negative', 'backwards', 'sequence', 'swap', 'missing_swap', 'config', 'anchor'])
def test_corrupted_evidence_rejected(tmp_path, mutation):
    _, rows = trace(tmp_path)
    if mutation == 'schema': rows[0]['schema'] = 9
    elif mutation == 'duplicate_shard': rows[2]['shard'] = 0
    elif mutation == 'missing_shard': del rows[2]
    elif mutation == 'negative': rows[1]['draws'] = -1
    elif mutation == 'backwards': rows[1]['end_ns'] = rows[1]['begin_ns']-1
    elif mutation == 'sequence': rows[3]['sequence'] = 1
    elif mutation == 'swap': rows[6]['closing_swap'] = 10; rows[7]['closing_swap'] = 10; rows[8]['closing_swap'] = 10
    elif mutation == 'missing_swap':
        for row in rows[6:9]: row['closing_swap'] = 12
    elif mutation == 'config': rows[1]['scheduling'] = 'interleaved'
    elif mutation == 'anchor': rows[1]['clock_anchor_ns'] += 1
    path = write_trace(tmp_path/'bad.csv', rows)
    with pytest.raises(ValueError):
        report.aggregate_frames(report.load_replay(path))


def test_truncated_row_is_not_a_zero_sample(tmp_path):
    path, _ = trace(tmp_path)
    with path.open('a') as handle: handle.write('1,batch,5,')
    with pytest.raises(ValueError): report.load_replay(path)


def test_direct_trace_has_no_prepared_or_shard_measurements(tmp_path):
    rows = []
    for sequence, swap in ((1, 10), (2, 11)):
        row = dict.fromkeys(HEADER, '')
        row.update(schema=1, kind='batch', sequence=sequence, closing_swap=swap,
                   api='vulkan', path='direct', shards=1, scheduling='contiguous', commands=1, draws=1,
                   direct_decode_ns=25, direct_arena_ns=3, shard=-1,
                   clock_anchor_ns=100, clock_anchor_utc_ns=1000)
        rows.append(row)
    result = report.aggregate_frames(report.load_replay(write_trace(tmp_path/'direct.csv', rows)))
    frame = result['frames'][0]
    assert frame['replay_ns'] == 28
    assert frame['sum_batch_max_shard_ns'] is None


def test_clock_anchor_excludes_warmup_and_crossing_frames(tmp_path):
    path, _ = trace(tmp_path)
    timeline = tmp_path/'timeline.csv'
    timeline.write_text('frame,stage,begin_ns,end_ns,busy_ns,blocked_ns\n'
                        '10,game,140000000,180000000,40000000,0\n'
                        '11,game,190000000,240000000,50000000,0\n'
                        '11,worker,195000000,238000000,40000000,3000000\n')
    windows = tmp_path/'windows.json'
    windows.write_text(json.dumps({'schema':1,'name':'fixture','windows':[
        {'scenario':'idle','begin_utc_ns':1_050_000_000,'end_utc_ns':1_150_000_000},
        {'scenario':'forward','begin_utc_ns':1_120_000_000,'end_utc_ns':1_200_000_000}]}))
    result = report.analyze(path, timeline, windows, require_populations=False)
    assert result['scenarios']['idle']['replay_ms']['samples'] == 1
    assert result['scenarios']['idle']['replay_ms']['mean'] == 5
    assert result['scenarios']['idle']['shards']['0']['elapsed_ms']['samples'] == 2
    assert result['scenarios']['idle']['shards']['0']['elapsed_ms']['mean'] == 1.25
    assert result['scenarios']['idle']['frame_interval_ms']['mean'] == 60
    assert result['scenarios']['forward']['replay_ms']['samples'] == 0


def test_paired_bootstrap_uses_process_pairs():
    result = report.paired_bootstrap([100]*20, [90]*20)
    assert result['pairs'] == 20
    assert result['reduction'] == pytest.approx(.1)
    assert result['ci95'] == pytest.approx([.1, .1])
    assert report.paired_bootstrap([100, 120], [90, 115], seed=7) == report.paired_bootstrap([100, 120], [90, 115], seed=7)
    for controls, candidates in (([], []), ([100], []), ([0], [1])):
        with pytest.raises(ValueError): report.paired_bootstrap(controls, candidates)


def test_matrix_pairs_processes_not_internal_rounds(tmp_path):
    runs = []
    for block in (1, 2):
        for variant, cycle in (('direct', 100), ('contiguous-4', 90)):
            path = tmp_path/f'{block}-{variant}.json'
            path.write_text(json.dumps({'schema':1,'checksum':31793585826180,
                                        'mode':'direct' if variant == 'direct' else 'prepared',
                                        'threads':1 if variant == 'direct' else 4,
                                        'scheduling':'contiguous','batch_draws':32,'rounds':10,'warmup_rounds':2,
                                        'samples':[{'cycle_ms':cycle}]*8}))
            runs.append({'id':path.stem,'block':block,'variant':variant,'profiled':False,
                         'status':'complete','exit_code':0,'scenario_files':{'isolated':str(path)}})
    manifest = tmp_path/'manifest.json'
    manifest.write_text(json.dumps({'schema':1,'phase':'Isolated','settings':{'batch_commands':32},'runs':runs}))
    value = report.analyze_matrix(manifest)
    assert len(value['comparisons']) == 1
    comparison = value['comparisons'][0]
    assert comparison['pairs'] == 2
    assert comparison['reduction'] == pytest.approx(.1)


def test_instrumented_matrix_requires_its_trace(tmp_path):
    manifest = tmp_path/'manifest.json'
    manifest.write_text(json.dumps({'schema':1,'phase':'Game','settings':{},'runs':[
        {'id':'broken','block':1,'variant':'direct','profiled':True,'status':'complete',
         'exit_code':0,'scenario_files':{}}]}))
    with pytest.raises(ValueError): report.analyze_matrix(manifest)


def test_clean_matrix_needs_no_trace_and_keeps_periodic_fps_label(tmp_path):
    runs = []
    for variant, fps in (('direct', 24), ('contiguous-4', 30)):
        window = tmp_path/f'{variant}.json'
        window.write_text(json.dumps({'schema':1,'windows':[
            {'scenario':'idle','begin_utc_ns':0,'end_utc_ns':20_000_000_000,'fps_samples':[fps]*10}]}))
        log = tmp_path/f'{variant}.log'
        mode, threads = ('direct', 1) if variant == 'direct' else ('prepared', 4)
        log.write_text(f'native renderer: replay mode={mode} api=vulkan threads={threads} scheduling=contiguous batch_limit=32\n')
        runs.append({'id':variant,'block':1,'variant':variant,'profiled':False,'status':'complete',
                     'exit_code':0,'scenario_files':{'windows':str(window),'log':str(log)}})
    manifest = tmp_path/'clean.json'
    manifest.write_text(json.dumps({'schema':1,'phase':'Game','settings':{'batch_commands':32},'runs':runs}))
    comparison = report.analyze_matrix(manifest)['comparisons'][0]
    assert comparison['metric'] == 'idle:periodic_fps_reciprocal_ms'
    assert comparison['reduction'] == pytest.approx(.2)


@pytest.mark.parametrize('field,value', [('mode','direct'),('threads',1),('scheduling','contiguous'),('batch_draws',256)])
def test_isolated_matrix_rejects_mislabeled_configuration(tmp_path, field, value):
    data = dict(schema=1,checksum=31793585826180,mode='prepared',threads=8,
                scheduling='interleaved',batch_draws=32,rounds=10,warmup_rounds=2,
                samples=[{'cycle_ms':90}]*8)
    data[field] = value
    path = tmp_path/'isolated.json';path.write_text(json.dumps(data))
    manifest = tmp_path/'matrix.json'
    manifest.write_text(json.dumps(dict(schema=1,phase='Isolated',settings={'batch_commands':32},runs=[
        dict(id='wrong',block=1,variant='interleaved-8',profiled=False,status='complete',exit_code=0,
             scenario_files={'isolated':str(path)})])))
    with pytest.raises(ValueError, match='configuration'): report.analyze_matrix(manifest)


def test_game_log_configuration_is_bound_to_variant_and_stratum(tmp_path):
    log = tmp_path/'game.log'
    log.write_text('native renderer: replay mode=direct api=vulkan threads=1 scheduling=contiguous batch_limit=256\n')
    with pytest.raises(ValueError, match='configuration'):
        report.validate_game_configuration(log, 'interleaved-8', 32)


def test_game_csv_configuration_must_also_match(tmp_path):
    _, rows = trace(tmp_path)
    for row in rows: row['scheduling'] = 'interleaved'
    replay = write_trace(tmp_path/'wrong.csv', rows)
    timeline = tmp_path/'timeline.csv'
    timeline.write_text('frame,stage,begin_ns,end_ns,busy_ns,blocked_ns\n'
                        '11,game,190000000,240000000,50000000,0\n'
                        '11,worker,195000000,238000000,40000000,3000000\n')
    log = tmp_path/'game.log'
    log.write_text('native renderer: replay mode=prepared api=vulkan threads=2 scheduling=contiguous batch_limit=32\n')
    windows = tmp_path/'windows.json'
    windows.write_text(json.dumps(dict(schema=1,windows=[dict(scenario='idle',begin_utc_ns=1_050_000_000,
                                                            end_utc_ns=1_150_000_000,fps_samples=[30]*10)])))
    manifest = tmp_path/'game.json'
    manifest.write_text(json.dumps(dict(schema=1,phase='Game',settings={'batch_commands':32},runs=[
        dict(id='wrong',block=1,variant='contiguous-2',profiled=True,status='complete',exit_code=0,
             scenario_files=dict(log=str(log),windows=str(windows),replay=str(replay),timeline=str(timeline)))])))
    with pytest.raises(ValueError, match='CSV effective configuration'): report.analyze_matrix(manifest)


@pytest.mark.parametrize('bad', ['empty', 'duplicate', 'negative', 'truncated', 'no_window_frames'])
def test_instrumented_analysis_rejects_unusable_timeline(tmp_path, bad):
    path, _ = trace(tmp_path)
    text = 'frame,stage,begin_ns,end_ns,busy_ns,blocked_ns\n'
    if bad != 'empty':
        text += '11,game,190000000,240000000,50000000,0\n11,worker,195000000,238000000,40000000,3000000\n'
    if bad == 'duplicate': text += '11,game,190000000,240000000,50000000,0\n'
    if bad == 'negative': text += '12,worker,1,2,-1,0\n'
    if bad == 'truncated': text += '12,worker,1,'
    timeline = tmp_path/'timeline.csv';timeline.write_text(text)
    begin,end = (2_000_000_000,3_000_000_000) if bad == 'no_window_frames' else (1_050_000_000,1_150_000_000)
    windows = tmp_path/'windows.json'
    windows.write_text(json.dumps(dict(schema=1,windows=[dict(scenario='idle',begin_utc_ns=begin,end_utc_ns=end)])))
    with pytest.raises(ValueError): report.analyze(path, timeline, windows)
