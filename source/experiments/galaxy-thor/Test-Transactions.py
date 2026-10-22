from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
JDK=Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin')
OUT=ROOT/'build/galaxy-integration/transaction-tests';OUT.mkdir(parents=True,exist_ok=True)
HERE=Path(__file__).parent
subprocess.run([str(JDK/'javac.exe'),'-d',str(OUT),*map(str,[HERE/'android/GalaxyInstall.java',HERE/'android/GalaxyMigration.java',HERE/'tests/Transactions.java'])],check=True)
result=subprocess.check_output([str(JDK/'java.exe'),'-cp',str(OUT),'dev.thor.galaxy.Transactions'],text=True)
(OUT/'result.txt').write_text(result);print(result)
