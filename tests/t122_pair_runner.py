"""T-122 regression-pair runner (the verifier half of the red/green pair).

Builds tests/test_sound_engine.cpp against whatever subject currently sits in
src/audio/, runs it, and requires exit code 0. The SAIPEN regression engine
runs this SAME command twice: once with the pre-fix subject restored from
.saipen/evidence/pre/ (which must FAIL), once with the repaired subject in
place (which must PASS). The runner is cwd-independent and emits standard
unittest output on stderr.
"""

import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "tests"
QT_BIN = Path(os.environ.get("QT_BIN", r"C:\Qt\6.8.0\msvc2022_64\bin"))


class T122RegressionPair(unittest.TestCase):
    maxDiff = None

    def test_defect_contracts(self):
        out_dir = ROOT / "build_t122_pair"
        out_dir.mkdir(exist_ok=True)
        build = subprocess.run(
            ["cmd", "/c", str(TESTS / "t122_pair_build.bat"), str(out_dir)],
            capture_output=True,
            text=True,
            cwd=str(ROOT),
            timeout=420,
        )
        print(build.stdout)
        print(build.stderr)
        self.assertEqual(build.returncode, 0, "pair oracle failed to compile")
        exe = out_dir / "t122_pair_test.exe"
        self.assertTrue(exe.exists(), "pair oracle binary missing after build")

        env = dict(os.environ)
        env["PATH"] = str(QT_BIN) + os.pathsep + env.get("PATH", "")
        env["QT_QPA_PLATFORM"] = "offscreen"
        run = subprocess.run(
            [str(exe)],
            capture_output=True,
            text=True,
            env=env,
            cwd=str(ROOT),
            timeout=300,
        )
        print(run.stdout)
        print(run.stderr)
        self.assertEqual(
            run.returncode,
            0,
            "defect contracts failed (exit %d) -- see stdout above" % run.returncode,
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
