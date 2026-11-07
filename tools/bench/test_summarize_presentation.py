import csv
import tempfile
import unittest
from pathlib import Path
from summarize_presentation import summarize

class PresentationSummaryTests(unittest.TestCase):
    def run_summary(self, records):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'presentation.csv'
            with path.open('w',newline='') as stream:
                writer=csv.DictWriter(stream,fieldnames=['accepted','source_serial','kind','return_ns','accepted_interval_ns','skipped_slots'])
                writer.writeheader()
                for serial,kind,ns,accepted in records:
                    writer.writerow(dict(accepted=accepted,source_serial=serial,kind=kind,return_ns=ns,accepted_interval_ns=250000000,skipped_slots=0))
            return summarize(path)

    def test_repaints_and_failed_present_are_not_unique_sources(self):
        report=self.run_summary([(1,'real',0,1),(1,'repeat',250000000,1),(99,'real',300000000,0),(2,'real',500000000,1),(2,'repeat',750000000,1),(3,'real',1000000000,1)])
        self.assertEqual(report['accepted_api_fps'],4)
        self.assertEqual(report['unique_source_api_fps'],2)
        self.assertEqual(report['unique_source_presentations'],3)
        self.assertEqual(report['unique_source_interval_stddev_ms'],0)
        self.assertEqual(report['unique_source_interval_p99_ms'],500)
        self.assertEqual(report['source_serial_gaps'],0)

    def test_empty_source_never_establishes_unique_cadence(self):
        report=self.run_summary([(0,'real',0,1),(0,'repeat',250000000,1)])
        self.assertEqual(report['unique_source_presentations'],0)
        self.assertIsNone(report['unique_source_api_fps'])
        self.assertIsNone(report['unique_source_interval_p99_ms'])

if __name__=='__main__':unittest.main()
