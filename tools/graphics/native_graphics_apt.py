"""Build the game's own graphics screen from the owner's EA APT assets.

Never modifies game/DATA/ui.AST. Original bytes are read locally; generated
archive/patch recipes belong under artifacts, never in source control.
"""
import argparse,copy,hashlib,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'analysis'))
from coop_apt_probe import Inspector,inspect_pair,SIMPLE,BRANCHES

SOURCE_SHA='9893245576351240f1362b7d9e8e8121a1a899dd16e605d9decb8096e08c177f'
def word(b,p):return struct.unpack_from('>I',b,p)[0]
def put(b,p,v):struct.pack_into('>I',b,p,v)
def fnv64(s):
 h=0xcbf29ce484222325
 for c in s.encode('ascii'):h=((h^c)*0x100000001b3)&0xffffffffffffffff
 return h

class Asset:
 def __init__(self,apt,const):
  self.data=bytearray(apt);self.const=bytearray(const);self.original=inspect_pair(apt,const);self.strings={};self.registers={c['value']:i for i,c in enumerate(self.original['constants']) if c['type']=='register'}
 def align(self,n=4):self.data.extend(bytes((-len(self.data))%n));return len(self.data)
 def append(self,data,alignment=4):p=self.align(alignment);self.data.extend(data);return p
 def string(self,text):
  if text not in self.strings:self.strings[text]=self.append(text.encode('ascii')+b'\0',1)
  return self.strings[text]
 def write(self,p,value):put(self.data,p,value)
 def replace_constant(self,old,new):
  for i,c in enumerate(self.original['constants']):
   if c['type']=='string' and c['value']==old:
    table=word(self.const,28);offset=len(self.const);self.const.extend(new.encode('ascii')+b'\0');put(self.const,table+i*8+4,offset)
 def register_constant(self,index):
  if index in self.registers:return self.registers[index]
  table,count=word(self.const,28),word(self.const,24)
  self.const.extend(bytes((-len(self.const))%4));new_table=len(self.const)
  self.const.extend(self.const[table:table+count*8]);self.const.extend(struct.pack('>II',4,index))
  put(self.const,24,count+1);put(self.const,28,new_table);self.registers[index]=count;return count
 def install_stream(self,index,emit):
  p=self.align();c=Code(self,p);emit(c);c.finish()
  old=self.original['streams'][index]['offset'];found=0
  # Timeline/control pointers only: never replace a matching integer in code.
  for char in self.original['movie']['characters']:
   for frame in char.get('frames',[]):
    for control in frame:
     if control.get('actions_offset')==old:
      self.write(control['offset']+(8 if control['type']==8 else 4),p);found+=1
     if control['type'] in (3,9) and control.get('flags',0)&128:
      event=word(self.data,control['offset']+60)
      for i in range(word(self.data,event)):
       at=word(self.data,event+4)+i*12+8
       if word(self.data,at)==old:self.write(at,p);found+=1
  if not found:raise ValueError('No exact stream pointer for '+str(index))
  return p

class Code:
 def __init__(self,asset,start):self.asset=asset;self.start=start;self.labels={};self.branches=[];self.refs=[]
 @property
 def at(self):return len(self.asset.data)
 def raw(self,b):self.asset.data.extend(b)
 def op(self,op):self.raw(bytes([op]))
 def align(self):self.asset.align()
 def u32(self,v):self.raw(struct.pack('>I',v&0xffffffff))
 def text(self,op,text):
  self.op(op);self.align();at=self.at;self.u32(0);self.refs.append((at,text))
 def push(self,v):
  if isinstance(v,str):self.text(0xa1,v)
  elif v is None:self.op(0x76)
  elif isinstance(v,bool):self.op(0x73 if v else 0x74)
  elif isinstance(v,float):self.op(0xb4);self.raw(struct.pack('>f',v))
  else:self.op(0xb7);self.u32(v)
 def reg(self,n):
  # This title's VM dispatch table ends at B8; B9 is data, not a handler.
  # Use the original Push/constant-register form instead of a newer APT opcode.
  index=self.asset.register_constant(n);self.op(0x96);self.align();self.u32(1)
  at=self.at;self.u32(0);self.refs.append((at,('register',index)))
 def store(self,n):self.op(0x87);self.align();self.u32(n);self.op(0x17)
 def member(self,obj,name):obj(self);self.push(name);self.op(0x4e)
 def call(self,obj,name,args=(),pop=False):
  for arg in reversed(args):arg(self) if callable(arg) else self.push(arg)
  self.push(len(args));obj(self);self.push(name);self.op(0x52)
  if pop:self.op(0x17)
 def assign(self,obj,name,value):
  obj(self);self.push(name);value(self) if callable(value) else self.push(value);self.op(0x4f)
 def label(self,name):
  if name in self.labels:raise ValueError('Duplicate label '+str(name))
  self.labels[name]=self.at
 def branch(self,op,target):self.op(op);self.align();at=self.at;self.u32(0);self.branches.append((at,target))
 def define(self,row,body):
  op=row['opcode'];self.op(op);self.align();name_at=self.at;self.u32(0);self.refs.append((name_at,row.get('function_name','')))
  params=row.get('params',[]);self.u32(len(params))
  if op==0x8e:self.raw(struct.pack('>HH',max(row.get('register_count',0),20),row.get('flags',297)))
  param_at=self.at;self.u32(0);size_at=self.at;self.u32(0);self.u32(0);self.u32(0)
  begin=self.at
  if callable(body):body(self)
  else:self.rows(body)
  self.asset.write(size_at,self.at-begin)
  if params:
   self.refs.append((param_at,('params',params,op)))
 def rows(self,rows,rename=None):
  rename=rename or {}
  for row in rows:
   self.label(row['offset']);op=row['opcode'];val=row.get('value')
   if isinstance(val,dict):val=val['value']
   if isinstance(val,str):val=rename.get(val,val)
   if op==0x88:continue # All dictionary operations below are expanded explicitly.
   if op in (0x8e,0x9b):self.define(row,row['body']);continue
   if op==0x96:
    for v in row['values']:
     if v['type']=='register':self.reg(v['value'])
     elif v['type']=='undefined':self.push(None)
     else:self.push(rename.get(v['value'],v['value']) if isinstance(v['value'],str) else v['value'])
   elif op in (0xa2,0xa3):self.push(val)
   elif op in (0xae,0xaf):
    if op==0xae:self.text(0xa4,val)
    else:self.push(val);self.op(0x4e)
   elif op in (0xb0,0xb1,0xb2,0xb3):
    self.push(val);self.op(0x3d if op in (0xb0,0xb1) else 0x52)
    if op in (0xb0,0xb2):self.op(0x17)
    else:self.op(0x1d)
   elif op in BRANCHES:self.branch(op,row['target'])
   elif op in (0x81,0x87,0x9f):self.op(op);self.align();self.u32(val)
   elif op in (0xa1,0xa4,0xa5,0xa6,0xa7,0x8b,0x8c):self.text(op,val)
   elif op==0x83:self.text(op,val);self.refs.append((self.at,row['target_name']));self.u32(0)
   elif op==0xb9:self.reg(val)
   elif op in (0xb4,0xb5,0xb6,0xb7):self.push(float(val) if op==0xb4 else val)
   elif op==0x77:self.op(op);self.u32(val)
   elif op in SIMPLE:self.op(op)
   else:raise ValueError('Unsupported emission '+hex(op))
 def finish(self):
  for at,target in self.branches:
   if target not in self.labels:raise ValueError('Unresolved branch '+str(target))
   self.asset.write(at,self.labels[target]-(at+4))
  # Strings and parameter metadata are outside executable streams/function bodies.
  for at,text in list(self.refs):
   if isinstance(text,tuple) and text[0]=='register':
    self.asset.write(at,self.asset.append(struct.pack('>I',text[1])))
   elif isinstance(text,tuple):
    _,params,op=text;p=self.asset.align();self.asset.write(at,p)
    for param in params:
     if op==0x8e:self.u32(param['register'])
     ref=self.at;self.u32(0);self.refs.append((ref,param['name']))
   else:self.asset.write(at,self.asset.string(text))
  for at,text in self.refs:
   if isinstance(text,str):self.asset.write(at,self.asset.string(text))

THIS=lambda c:c.reg(1)
GLOBAL=lambda c:c.op(0x71)
def member(obj,name):return lambda c:c.member(obj,name)
def global_(name):return member(GLOBAL,name)
def control(name):return member(THIS,name)
def call(obj,name,*args):return lambda c:c.call(obj,name,args)
def native(row,field,set_=False):return call(global_('StateAdapter'),'PauseGame',21331 if set_ else 21330,row,field)
def key(name):return member(global_('INPUTCODE'),name)
def condition(c,left,right,where):left(c);right(c) if callable(right) else c.push(right);c.op(0x49);c.op(0x12);c.branch(0x9d,where)

def class_stream(asset,mutators,rename=None,extra=None):
 rows=copy.deepcopy(asset.original['streams'][3]['instructions']);rename=rename or {}
 def emit(c):
  # Re-emit each original structural instruction; custom functions keep original ABI.
  for row in rows:
   if row['opcode']==0 and extra:
    for method,body in extra.items():
     c.member(global_(rename.get('ScrGameplayOpt','ScrGameplayOpt')),'prototype');c.push(method)
     c.define({'opcode':0x8e,'flags':297,'register_count':20,'params':[]},body);c.op(0x4f)
   name=row.get('assigned_member')
   if name in mutators and row.get('body_size',0)>0:
    c.label(row['offset']);c.define(row,mutators[name]);continue
   # Dictionary values retain text. Rename class symbols recursively first.
   def ren(r):
    r=copy.deepcopy(r)
    if isinstance(r.get('value'),dict) and isinstance(r['value'].get('value'),str):r['value']['value']=rename.get(r['value']['value'],r['value']['value'])
    if isinstance(r.get('value'),str):r['value']=rename.get(r['value'],r['value'])
    if 'body' in r:r['body']=[ren(x) for x in r['body']]
    return r
   c.rows([ren(row)],rename)
 asset.install_stream(3,emit)


def own_stream(asset,emit):
 p=asset.align();code=Code(asset,p);emit(code);code.finish();return p

def clip_init(asset,tab):
 def emit(c):
  values={'mArrowWidth':200,'mLabel':'','isWrapping':False,'m_bEnabled':True,
    'm_refFM':'_global.gFM','m_initiallySelected':tab==0,'m_tabIndex':tab,'m_bVisible':True}
  for name,value in values.items():c.push(name);c.push(value);c.op(0x1d)
  c.push('mOptionsArray');c.push('On');c.push('Off');c.push(2);c.op(0x42);c.op(0x1d);c.op(0)
 action=own_stream(asset,emit)
 p=asset.append(struct.pack('>II',1,0));ev=asset.append(struct.pack('>III',0x40000,0,action));asset.write(p+4,ev);return p

def graphics_screen(apt,const):
 a=Asset(apt,const)
 for old,new in [('ScrGameplayOpt','ScrGraphicsOpt')]:a.replace_constant(old,new)
 # FlowMgr attaches the screen by exported movie/class name as well as URL.
 # Rename the native export registrations, including the package class symbol.
 root=a.original['movie']['offset'];exports=word(a.data,root+60)
 for i,export in enumerate(a.original['movie']['exports']):
  name=export['name'].replace('ScrGameplayOpt','ScrGraphicsOpt')
  if name!=export['name']:a.write(exports+i*8,a.string(name))
 sprite=next(c for c in a.original['movie']['characters'] if c['index']==2)
 original=[c for c in sprite['frames'][0] if c['type']==3][0]
 controls=[sprite['frames'][0][0]['offset']]
 for i in range(6):
  p=a.append(a.data[original['offset']:original['offset']+64]);a.write(p+8,1+i*4)
  a.data[p+16:p+40]=struct.pack('>6f',1,0,0,1,-394,214+i*33 if i<5 else 420)
  a.write(p+52,a.string('srRow'+str(i)));a.write(p+60,clip_init(a,i));controls.append(p)
 table=a.append(struct.pack('>'+'I'*len(controls),*controls));frames=word(a.data,sprite['offset']+20)
 a.write(frames,len(controls));a.write(frames+4,table)
 def row(i):return control('srRow'+str(i))
 def index(i):return lambda c:(c.member(THIS,'srPage'),c.push(5),c.op(0xc),c.push(i),c.op(0xa))
 def refresh(c):
  for i in range(5):
   idx=index(i);idx(c);c.store(6)
   c.reg(6);c.push(11);c.op(0xf);c.branch(0xb8,('hide',i))
   c.assign(row(i),'_visible',True);c.assign(row(i),'m_bEnabled',True);c.assign(row(i),'srOption',lambda c:c.reg(6))
   c.call(row(i),'SetLabel',[native(lambda c:c.reg(6),3)],pop=True)
   c.call(row(i),'SetData',[call(native(lambda c:c.reg(6),4),'split','|')],pop=True)
   c.call(row(i),'SetIndex',[native(lambda c:c.reg(6),0)],pop=True)
   c.branch(0x99,('done',i));c.label(('hide',i));c.assign(row(i),'_visible',False);c.assign(row(i),'m_bEnabled',False);c.label(('done',i))
  c.call(row(5),'SetIndex',[member(THIS,'srPage')],pop=True);c.push(None);c.op(0x3e)
 def init(c):
  native(-1,0)(c);c.op(0x17);c.assign(THIS,'srPage',0);c.call(GLOBAL,'SetScreenTitle',['GRAPHICS OPTIONS'],pop=True)
  c.call(GLOBAL,'ShowHints',[call(global_('LocalizationAdapter'),'GetLocalizedText','Generic_BACK'),'B'],pop=True)
  for i in range(6):c.call(row(i),'SetWidth',[200],pop=True)
  c.call(row(5),'SetLabel',['PAGE'],pop=True);c.call(row(5),'SetData',[call(lambda c:c.push('1 / 3|2 / 3|3 / 3'),'split','|')],pop=True)
  c.call(THIS,'SrRefresh',pop=True);c.push(None);c.op(0x3e)
 def handle(c):
  condition(c,lambda c:c.reg(3),key('TRIANGLE'),'notback')
  c.call(GLOBAL,'playSound',[member(global_('SOUND'),'CLOSE')],pop=True)
  c.call(global_('FlowMgr'),'SwitchScreen',[member(global_('SCREEN'),'PAUSEMENU')],pop=True);c.push(True);c.op(0x3e);c.label('notback')
  condition(c,lambda c:c.reg(3),key('START'),'notstart')
  c.call(GLOBAL,'playSound',[member(global_('SOUND'),'CLOSE')],pop=True)
  c.call(global_('FlowMgr'),'ChangeInterfaceState',['IG'],pop=True);c.push(True);c.op(0x3e);c.label('notstart')
  c.call(global_('gFM'),'handleDefaultInputs',[lambda c:c.reg(3)],pop=True)
  c.reg(3);key('LEFT')(c);c.op(0x49);c.reg(3);key('RIGHT')(c);c.op(0x49);c.op(0x11);c.branch(0xb8,'return')
  c.call(global_('gFM'),'getSelectedItem');c.store(4)
  condition(c,member(lambda c:c.reg(4),'_name'),'srRow5','option')
  c.assign(THIS,'srPage',call(lambda c:c.reg(4),'GetIndex'));c.branch(0x99,'refresh')
  c.label('option');native(member(lambda c:c.reg(4),'srOption'),call(lambda c:c.reg(4),'GetIndex'),True)(c);c.op(0x17)
  c.label('refresh');c.call(THIS,'SrRefresh',pop=True);c.label('return');c.push(True);c.op(0x3e)
 def exit_(c):
  native(-1,1)(c);c.op(0x17)
  for i in range(6):c.call(row(i),'OnExit',pop=True)
  c.call(global_('Object'),'registerClass',['ScrGraphicsOpt',lambda c:c.op(0x75)],pop=True)
  GLOBAL(c);c.push('ScrGraphicsOpt');c.op(0x3a);c.op(0x17);c.push(None);c.op(0x3e)
 class_stream(a,{'InitGui':init,'HandleInput':handle,'ExitScreen':exit_},{'ScrGameplayOpt':'ScrGraphicsOpt'},extra={'SrRefresh':refresh})
 return bytes(a.data),bytes(a.const)

def pause_screen(apt,const):
 a=Asset(apt,const);a.replace_constant('btn9','btn10')
 sprite=next(c for c in a.original['movie']['characters'] if c['index']==2)
 controls=[c['offset'] for c in sprite['frames'][0]]
 quit_=next(c for c in sprite['frames'][0] if c.get('instance_name')=='btn9')
 p=quit_['offset'];a.write(p+52,a.string('btn10'));a.data[p+36:p+40]=struct.pack('>f',440)
 # Quit's original constructor tabIndex8 becomes9; Graphcis gets8 below.
 event=word(a.data,p+60);action=word(a.data,word(a.data,event+4)+8)
 st=next(s for s in a.original['streams'] if s['offset']==action)
 def quit_ctor(c):
  rows=copy.deepcopy(st['instructions'])
  for j,r in enumerate(rows):
   value=r.get('value');value=value.get('value') if isinstance(value,dict) else value
   if value=='m_tabIndex' and j+1<len(rows) and rows[j+1].get('value')==8:rows[j+1]['value']=9
  c.rows(rows)
 new_action=own_stream(a,quit_ctor);a.write(word(a.data,event+4)+8,new_action)
 new=a.append(a.data[p:p+64]);a.write(new+8,37);a.write(new+52,a.string('btn9'));a.data[new+36:new+40]=struct.pack('>f',382)
 # BasicButton constructor settings, distinct tab slot8, same original graphic.
 def ctor(c):
  for name,value in {'mButtonText':'','m_bEnabled':True,'m_refFM':'_global.gFM','m_initiallySelected':False,'m_tabIndex':8,'m_bVisible':True}.items():c.push(name);c.push(value);c.op(0x1d)
  c.op(0)
 act=own_stream(a,ctor);ev=a.append(struct.pack('>III',0x40000,0,act));event_new=a.append(struct.pack('>II',1,ev));a.write(new+60,event_new)
 controls.append(new);table=a.append(struct.pack('>'+'I'*len(controls),*controls));frames=word(a.data,sprite['offset']+20);a.write(frames,len(controls));a.write(frames+4,table)
 methods={r['assigned_member']:r for r in a.original['streams'][3]['instructions'] if r.get('body_size',0)>0 and r.get('assigned_member')}
 def old_body(c,name):c.rows(methods[name]['body'],{'btn9':'btn10'})
 def init(c):
  old_body(c,'InitGui');c.assign(global_('SCREEN'),'GRAPHICS_OPT','screens/misc/ScrGraphicsOpt')
  c.call(control('btn9'),'SetWidth',[790],pop=True);c.call(control('btn9'),'SetText',['GRAPHICS OPTIONS'],pop=True)
 def handle(c):
  condition(c,lambda c:c.reg(4),key('CROSS'),'ordinary')
  condition(c,member(call(global_('gFM'),'getSelectedItem'),'_name'),'btn9','ordinary')
  c.assign(GLOBAL,'PauseSelectedItem','btn9');c.call(GLOBAL,'playSound',[member(global_('SOUND'),'OPEN')],pop=True)
  c.call(global_('FlowMgr'),'SwitchScreen',[member(global_('SCREEN'),'GRAPHICS_OPT')],pop=True);c.push(True);c.op(0x3e)
  c.label('ordinary');old_body(c,'HandleInput')
 def exit_(c):c.call(control('btn9'),'OnExit',pop=True);old_body(c,'ExitScreen')
 class_stream(a,{'InitGui':init,'HandleInput':handle,'ExitScreen':exit_})
 return bytes(a.data),bytes(a.const)

def archive_records(data):
 if data[:8]!=b'BGFA1.05':raise ValueError('Unsupported BGFA')
 count,table=struct.unpack_from('<II',data,12);_,flags16,hashsize,offs,size,delta,shift,_=data[32:40];prefix,namesize=struct.unpack_from('<II',data,40)
 stride=(2 if flags16 else 1)+hashsize+offs+size+delta+namesize;start=table+prefix*4
 if count>10000 or start+count*stride>len(data):raise ValueError('Invalid BGFA directory')
 records=[]
 for i in range(count):
  at=start+i*stride;record=bytearray(data[at:at+stride]);cursor=(2 if flags16 else 1)+hashsize
  position=int.from_bytes(record[cursor:cursor+offs],'little')<<shift;length=int.from_bytes(record[cursor+offs:cursor+offs+size],'little')
  if position+length>len(data):raise ValueError('Invalid BGFA range')
  records.append({'name':record[-namesize:].split(b'\0')[0].decode('ascii') if namesize else '', 'record':record,'data':bytes(data[position:position+length])})
 return records,(table,prefix,namesize,stride,flags16,hashsize,offs,size,delta,shift)

def rebuild_archive(data,changes,new_entry=None):
 records,layout=archive_records(data);table,prefix,namesize,stride,flags16,hashsize,offs,size,delta,shift=layout
 result=bytearray(data[:table]);offset_start=(2 if flags16 else 1)+hashsize
 if new_entry:
  name,payload=new_entry;record=bytearray(next(r['record'] for r in records if r['name']=='SCRGAMEPLAYOPT'));record[-namesize:]=name.encode().ljust(namesize,b'\0')
  record[2:2+hashsize]=fnv64(name).to_bytes(hashsize,'little');records.append({'name':name,'record':record,'data':payload})
 for i,r in enumerate(records):
  if i not in changes and not (new_entry and i==len(records)-1):continue
  payload=changes.get(i,r['data']);result.extend(bytes((-len(result))%(1<<shift)));position=len(result);result.extend(payload)
  r['record'][offset_start:offset_start+offs]=(position>>shift).to_bytes(offs,'little')
  r['record'][offset_start+offs:offset_start+offs+size]=len(payload).to_bytes(size,'little')
  if delta:r['record'][offset_start+offs+size:offset_start+offs+size+delta]=bytes(delta)
 result.extend(bytes((-len(result))%4));directory=len(result);result.extend(data[table:table+prefix*4]);result.extend(b''.join(r['record'] for r in records))
 struct.pack_into('<I',result,12,len(records));struct.pack_into('<I',result,16,directory);struct.pack_into('<I',result,24,prefix*4+len(records)*stride)
 return bytes(result)

def transform(source):
 if hashlib.sha256(source).hexdigest()!=SOURCE_SHA:raise ValueError('UI archive identity differs; original preserved')
 records,_=archive_records(source);byname={r['name']:i for i,r in enumerate(records)}
 def pair(name,fn):
  package=records[byname[name]]['data'];inner,_=archive_records(package)
  c=next(i for i,r in enumerate(inner) if r['data'].startswith(b'Apt constant file'))
  a=next(i for i,r in enumerate(inner) if r['data'].startswith(b'Apt Data:'))
  apt,const=fn(inner[a]['data'],inner[c]['data']);inspect_pair(apt,const)
  return rebuild_archive(package,{a:apt,c:const})
 pause=pair('SCRPAUSEMENU',pause_screen);graphics=pair('SCRGAMEPLAYOPT',graphics_screen)
 return rebuild_archive(source,{byname['SCRPAUSEMENU']:pause},('SCRGRAPHICSOPT',graphics))

def write_recipe(source,result,path):
 import xxhash
 original_records,layout=archive_records(source);table=layout[0]
 # Only small source packages/directories are indexed; the 90MB archive prefix
 # is copied directly. Reused EA payload is referenced by original file offset.
 ranges=[(table,len(source))]
 for r in original_records:
  if r['name'] in ('SCRPAUSEMENU','SCRGAMEPLAYOPT'):
   raw=r['data'];at=source.find(raw);ranges.append((at,at+len(raw)))
 lookup={}
 for start,end in ranges:
  for at in range(start,end-7):
   key=source[at:at+8];bucket=lookup.setdefault(key,[])
   if len(bucket)<32:bucket.append(at)
 literals=bytearray();ops=[]
 def emit(at,n,data=None):
  if not n:return
  if data is None:ops.append((at,n,0))
  else:
   offset=len(literals);literals.extend(data);ops.append((0xffffffff,n,offset))
 # Header fields differ; preserve all other prefix bytes without duplicating them.
 at=0
 while at<64:
  same=result[at]==source[at];end=at+1
  while end<64 and (result[end]==source[end])==same:end+=1
  emit(at,end-at,None if same else result[at:end]);at=end
 emit(64,table-64)
 pos=table;pending=bytearray()
 while pos<len(result):
  best_at=best=0
  for at in lookup.get(result[pos:pos+8],[]):
   size=8
   while pos+size+64<=len(result) and at+size+64<=len(source) and result[pos+size:pos+size+64]==source[at+size:at+size+64]:size+=64
   while pos+size<len(result) and at+size<len(source) and result[pos+size]==source[at+size]:size+=1
   if size>best:best_at,best=at,size
  if best>=16:
   emit(0,len(pending),pending);pending.clear();emit(best_at,best);pos+=best
  else:pending.append(result[pos]);pos+=1
 emit(0,len(pending),pending)
 rebuilt=bytearray()
 for at,n,literal in ops:rebuilt.extend(literals[literal:literal+n] if at==0xffffffff else source[at:at+n])
 if rebuilt!=result:raise ValueError('Patch recipe reconstruction mismatch')
 lines=['// Generated metadata/port bytecode; original asset runs are copied from the owner archive.','#pragma once','#include <cstdint>','namespace superman_returns::graphics::apt_recipe {',
  'struct Op {uint32_t source,length,literal;};',
  f'inline constexpr char kSourceHash[]="{xxhash.xxh3_128_hexdigest(source)}";',
  f'inline constexpr char kOutputHash[]="{xxhash.xxh3_128_hexdigest(result)}";',
  f'inline constexpr uint32_t kSourceSize={len(source)},kOutputSize={len(result)};',
  'inline constexpr uint8_t kLiterals[]={']
 for i in range(0,len(literals),24):lines.append(','.join(str(v) for v in literals[i:i+24])+',')
 lines+=['};','inline constexpr Op kOps[]={']
 lines += ['{'+','.join(str(v)+'u' for v in op)+'},' for op in ops]
 lines+=['};','}'];path.parent.mkdir(parents=True,exist_ok=True);path.write_text('\n'.join(lines)+'\n')
 return {'operations':len(ops),'literal_bytes':len(literals),'source_bytes':len(source)}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,default=Path('game/DATA/ui.AST'));p.add_argument('--output',type=Path,required=True);p.add_argument('--recipe-header',type=Path)
 a=p.parse_args()
 if a.source.resolve()==a.output.resolve():p.error('Output must differ from original')
 source=a.source.read_bytes();result=transform(source);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(result)
 report={'source_sha256':SOURCE_SHA,'output_sha256':hashlib.sha256(result).hexdigest(),'output':str(a.output.resolve()),'native_apt':True,'execution_observed':False}
 if a.recipe_header:report['recipe']=write_recipe(source,result,a.recipe_header)
 a.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
