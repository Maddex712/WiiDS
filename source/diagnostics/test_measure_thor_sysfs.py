"""Host-only fixtures for Measure-Thor's bounded sysfs discovery/parsing."""
import importlib.util
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location("measure_thor", Path(__file__).with_name("Measure-Thor.py"))
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)


def result(text="", code=0):
    return {"returncode": code, "stdout": text}


class SysfsTests(unittest.TestCase):
    def test_discovery_limits_families_and_rejects_paths(self):
        names = ["battery", "cpuss-1", "gpuss-1", "cpu-1-0", "cpuss-0", "gpuss-0"]
        names += [f"cpu-0-{n}" for n in range(10)]
        lines = [f"thermal\t/sys/class/thermal/thermal_zone{i}/temp\t{name}" for i, name in enumerate(names)]
        lines += ["thermal\t/tmp/injected;bad/temp\tcpuss-9",
                  "thermal\t/sys/class/thermal/thermal_zone99/temp\tcpuss-9;bad",
                  "cpu_frequency\t/sys/devices/system/cpu/cpufreq/policy7/scaling_cur_freq",
                  "cpu_frequency\t/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq"]
        selected = measure.parse_sysfs_discovery(result("\n".join(lines)))["fields"]
        temperatures = [s for s in selected if s["kind"] == "temperature"]
        self.assertEqual(8, len(temperatures))
        self.assertEqual(["cpuss-0", "cpuss-1", "gpuss-0", "gpuss-1"], [s["name"] for s in temperatures[:4]])
        self.assertEqual(["policy0", "policy7"], [s["name"] for s in selected if s["kind"] == "cpu_frequency"])
        self.assertNotIn("battery", [s["name"] for s in selected])
        self.assertNotIn("injected", measure.sysfs_sample_script(selected))

    def test_units_actual_sensor_values_and_counter_pair(self):
        fields = measure.parse_sysfs_discovery(result(
            "thermal\t/sys/class/thermal/thermal_zone31/temp\tcpuss-0\n"
            "cpu_frequency\t/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq\n"))["fields"]
        readings = measure.parse_sysfs_sample(result(
            "0\tok\t83500\n1\tok\t1459200\n2\tok\t680000000\n3\tok\t822203 1003216\n"), fields)["readings"]
        self.assertEqual(83.5, readings[0]["temperature_c"])
        self.assertEqual((83500, "millidegree_C"), (readings[0]["raw_value"], readings[0]["raw_unit"]))
        self.assertEqual((1459200, "kHz"), (readings[1]["raw_value"], readings[1]["raw_unit"]))
        self.assertEqual((680000000, "Hz"), (readings[2]["raw_value"], readings[2]["raw_unit"]))
        self.assertEqual([822203, 1003216], readings[3]["raw_value"])
        self.assertNotIn("utilization", readings[3])

    def test_unreadable_missing_malformed_and_transport_failure(self):
        fields = [dict(item) for item in measure.GPU_SYSFS_FIELDS]
        sample = measure.parse_sysfs_sample(result("0\tunavailable\n1\tok\tnot-a-counter\n"), fields)
        self.assertEqual(["unavailable", "invalid"], [r["status"] for r in sample["readings"]])
        missing = measure.parse_sysfs_sample(result(""), fields)
        self.assertTrue(all(r["status"] == "unavailable" for r in missing["readings"]))
        failed = measure.parse_sysfs_sample(result("0\tok\t123\n", code=None), fields)
        self.assertEqual("unavailable", failed["status"])
        self.assertTrue(all(r["status"] == "unavailable" for r in failed["readings"]))
        self.assertEqual("unavailable", measure.parse_sysfs_discovery(result(code=1))["status"])


if __name__ == "__main__":
    unittest.main()
