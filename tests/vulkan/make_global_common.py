"""Generate the project-owned GPU fixture's alternate common header."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/shaders'))
from vulkan_global_contract import prepare_common
source,target=map(Path,sys.argv[1:])
target.write_text(prepare_common(source.read_text(encoding='utf-8')),encoding='utf-8')
