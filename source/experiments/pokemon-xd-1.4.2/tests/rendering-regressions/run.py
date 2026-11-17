from pathlib import Path
import argparse,hashlib,json,os,subprocess
parser=argparse.ArgumentParser(description='Compile actual XD overlay functions with synthetic regression harnesses')
parser.add_argument('--cxx',default='clang++');parser.add_argument('--zig',action='store_true');parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args();tests=Path(__file__).resolve().parent;native=tests.parents[1]/'native';out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
def body(text,marker):
 start=text.index(marker);brace=text.index('{',start);i=brace+1;depth=1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 if marker.startswith('struct '):i+=1
 return text[start:i]
probe=(native/'xd_presentation.cpp').read_text(encoding='utf-8');surface=(native/'xd_lower_presenter.inc').read_text(encoding='utf-8');pipeline=(native/'aurora/gfx/pipeline_cache.cpp').read_text(encoding='utf-8');present=(native/'aurora/gfx/present_target.cpp').read_text(encoding='utf-8')
includes={'probe_body.inc':body(probe,'extern "C" void WiidsXD_TopProbe()'),'surface_state.inc':surface[surface.index('std::mutex xdSurfaceMutex;'):surface.index('uint64_t xdMeshRevision')],'surface_setter.inc':body(surface,'extern "C" void WiidsXD_LowerWindow('),'actual_notify.inc':body(pipeline,'static void notify_pipeline_ready('),'actual_state.inc':body(present,'struct TargetState {'),'actual_submit.inc':body(present,'void submit(const Request& request)')}
for name,text in includes.items():(out/name).write_text(text)
scenarios={'probe_test':['cadence','inactive','sky_stale','brightness_stale','fresh','partial','old_mode','snapshot','unsupported','mode_fade'],'surface_test':['duplicate','recovery'],'wait_test':[''],'present_test':['']};results={}
for name,cases in scenarios.items():
 exe=out/(name+('.exe' if os.name=='nt' else ''))
 command=[args.cxx]+(['c++'] if args.zig else [])+['-std=c++20','-O1','-I'+str(out),str(tests/(name+'.cpp')),'-o',str(exe)]
 compile=subprocess.run(command,capture_output=True);(out/(name+'-compile.log')).write_bytes(compile.stdout+compile.stderr)
 assert compile.returncode==0,compile.stderr.decode(errors='replace')
 for case in cases:
  run=subprocess.run([str(exe)]+([case] if case else []),capture_output=True,timeout=30)
  output=(run.stdout+run.stderr).decode(errors='replace');print(name,case,output.strip(),flush=True)
  results[name+('/'+case if case else '')]={'exit_code':run.returncode,'output':output}
  assert run.returncode==0,output
(out/'results.json').write_text(json.dumps({'cases':results,'extracted_body_sha256':{n:hashlib.sha256(s.encode()).hexdigest() for n,s in includes.items()}},indent=2))
print('PASS all 14 source regression scenarios',flush=True)
