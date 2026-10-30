"""Compare a --linear-output DLSS capture with the actual presented pixels.

Requires numpy and Pillow. The normal display-gamma capture is not comparable
directly: this check intentionally requires gamma disabled by the probe.
"""
import argparse,json,pathlib,struct
import numpy as np
from PIL import Image

def rgb(path):
    data=path.read_bytes()
    width,height,fmt,pitch=struct.unpack('<4I',data[:16])
    if len(data)!=16+pitch*height: raise ValueError(f'Unexpected dump size: {path}')
    if fmt==28: # DXGI_FORMAT_R8G8B8A8_UNORM
        result=np.frombuffer(data,dtype=np.uint8,offset=16).reshape(height,pitch)
        return result[:,:width*4].reshape(height,width,4)[:,:,:3].astype(float)/255
    if fmt==24: # DXGI_FORMAT_R10G10B10A2_UNORM, not four independent bytes
        result=np.frombuffer(data,dtype='<u4',offset=16).reshape(height,pitch//4)[:,:width]
        return np.stack([result&1023,(result>>10)&1023,(result>>20)&1023],axis=2).astype(float)/1023
    raise ValueError(f'Unsupported DXGI format {fmt}: {path}')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=pathlib.Path)
    a=p.parse_args()
    files=list(a.directory.glob('*output_1280x720.raw'))
    if len(files)!=1: p.error('Exactly one presentation dump is required')
    output=files[0]; prefix=output.name.removesuffix('output_1280x720.raw')
    actual=rgb(output); upscaled=rgb(a.directory/(prefix+'dlss_upscaled.raw'))
    if actual.shape!=upscaled.shape: raise ValueError('Presentation and NGX dimensions differ')
    error=np.abs(actual-upscaled)*255
    matched=float(np.mean(np.max(error,axis=2)<=2)*100)
    report={'width':actual.shape[1],'height':actual.shape[0],
            'mean_error_in_8bit_units':float(error.mean()),'percent_pixels_within_2':matched,
            'valid':matched>=99.5,'requires_linear_output':True}
    (a.directory/'pixel_check.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    Image.fromarray(np.uint8(np.rint(np.clip(actual,0,1)*255))).save(a.directory/'presented.png')
    print(json.dumps(report))
    if not report['valid']: raise SystemExit(1)

if __name__=='__main__': main()
