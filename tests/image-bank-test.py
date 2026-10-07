import importlib.util,struct,tempfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('bank',Path(__file__).resolve().parents[1]/'tools/image-bank-info.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='aamod-bank-',dir=Path(__file__).resolve().parents[1]/'out') as directory:
    path=Path(directory)/'bank.dat';offset=0x8c5f0
    with path.open('wb') as stream:stream.write(struct.pack('<II',offset,8));stream.seek(offset);stream.write(struct.pack('<4H',17,23,20,24))
    assert module.image_info(path,0)=={'index':0,'width':17,'height':23,'bufferWidth':20,'bufferHeight':24,'storedBytes':8}
    for index in [-1,module.IMAGE_COUNT]:
        try:module.image_info(path,index)
        except ValueError:pass
        else:raise AssertionError('索引边界未拒绝')
    path.write_bytes(struct.pack('<II',0xffffffff,8))
    try:module.image_info(path,0)
    except ValueError:pass
    else:raise AssertionError('越界记录未拒绝')
print('image bank metadata contracts passed')
