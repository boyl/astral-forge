"""离线检查原生图片 ID 与 Mod 自有 PNG 的绑定；不加载或修改游戏。"""
import argparse
import hashlib
import importlib.util
import json
import struct
import zlib
from pathlib import Path

_spec = importlib.util.spec_from_file_location('image_bank', Path(__file__).with_name('image-bank-info.py'))
bank = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bank)
SCHEMA = 1
MAX_PNG = 32 * 1024 * 1024
MAX_DECODED = 64 * 1024 * 1024


def file_hash(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def owned_file(root, relative):
    if not isinstance(relative, str) or not relative or ':' in relative or '\\' in relative:
        raise ValueError('资源路径必须为非空、使用 / 的相对路径')
    path = Path(relative)
    if path.anchor or '..' in path.parts:
        raise ValueError('资源路径不能跳出插件目录')
    resolved = (root / path).resolve(strict=True)
    if not resolved.is_relative_to(root.resolve(strict=True)) or not resolved.is_file():
        raise ValueError('资源文件解析后不属于插件目录')
    return resolved


def png_info(data):
    """当前计划只接受非交错 RGBA8；完整像素解码由 PNG 服务负责。"""
    if len(data) > MAX_PNG or data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('不是允许大小内的 PNG')
    cursor = 8
    dimensions = None
    compressed = bytearray()
    idat_seen = False
    idat_closed = False
    palette_seen = False
    ended = False
    while cursor < len(data):
        if cursor + 12 > len(data):
            raise ValueError('PNG 块头不完整')
        size, kind = struct.unpack_from('>I4s', data, cursor)
        if not all(65 <= value <= 90 or 97 <= value <= 122 for value in kind) or kind[2] & 32:
            raise ValueError('PNG 块类型无效')
        end = cursor + 12 + size
        if end > len(data):
            raise ValueError('PNG 块越界')
        payload = data[cursor + 8:end - 4]
        crc = struct.unpack_from('>I', data, end - 4)[0]
        if zlib.crc32(kind + payload) != crc:
            raise ValueError('PNG CRC 不匹配')
        if dimensions is None and kind != b'IHDR':
            raise ValueError('PNG 首块必须是 IHDR')
        if kind == b'IHDR':
            if dimensions is not None or size != 13:
                raise ValueError('PNG IHDR 无效')
            width, height, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', payload)
            if not (0 < width <= 8192 and 0 < height <= 8192) or width * height * 4 > MAX_DECODED:
                raise ValueError('PNG 尺寸超过限额')
            if (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
                raise ValueError('替换计划当前只接受非交错 RGBA8 PNG')
            dimensions = (width, height)
        elif kind == b'IDAT':
            if idat_closed:
                raise ValueError('PNG IDAT 不连续')
            idat_seen = True
            compressed.extend(payload)
        elif kind == b'PLTE':
            if palette_seen or idat_seen or not 0 < size <= 768 or size % 3:
                raise ValueError('PNG 可选调色板无效')
            palette_seen = True
        elif kind == b'IEND':
            if size or end != len(data) or not compressed:
                raise ValueError('PNG 结束块或图像数据无效')
            ended = True
            break
        else:
            if idat_seen:
                idat_closed = True
            if not kind[0] & 32:
                raise ValueError('不支持的 PNG 关键块')
        cursor = end
    if not ended:
        raise ValueError('PNG 缺少 IEND')
    width, height = dimensions
    expected = (width * 4 + 1) * height
    decoder = zlib.decompressobj()
    try:
        scanlines = decoder.decompress(compressed, expected + 1)
    except zlib.error as error:
        raise ValueError('PNG 压缩数据无效') from error
    if len(scanlines) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError('PNG 解压长度或压缩流无效')
    stride = width * 4 + 1
    if any(scanlines[row * stride] > 4 for row in range(height)):
        raise ValueError('PNG 行过滤类型无效')
    return {'width': width, 'height': height, 'format': 'RGBA8', 'sha256': hashlib.sha256(data).hexdigest()}


def prepare(bank_path, mod_root, request):
    if (not isinstance(request, dict) or set(request) != {'schema', 'replacements'}
            or type(request['schema']) is not int or request['schema'] != SCHEMA):
        raise ValueError('替换请求 schema 无效')
    replacements = request['replacements']
    if not isinstance(replacements, list) or not 1 <= len(replacements) <= 16:
        raise ValueError('替换计划需包含 1..16 个绑定')
    seen = set()
    bindings = []
    for item in replacements:
        if not isinstance(item, dict) or set(item) != {'imageId', 'png'}:
            raise ValueError('绑定仅接受 imageId 与 png')
        index = item['imageId']
        if type(index) is not int or index in seen:
            raise ValueError('图片 ID 必须为唯一整数')
        seen.add(index)
        original = bank.image_info(bank_path, index)
        path = owned_file(mod_root, item['png'])
        if path.stat().st_size > MAX_PNG:
            raise ValueError('PNG 文件超过限额')
        png = png_info(path.read_bytes())
        if (png['width'], png['height']) != (original['width'], original['height']):
            raise ValueError(f'图片 {index} 的 PNG 尺寸与原生逻辑尺寸不一致')
        bindings.append({'imageId': index, 'png': item['png'], 'original': original, 'replacement': png})
    return {'schema': SCHEMA, 'profileSha256': bank.PROFILE_SHA,
            'bankSha256': file_hash(bank_path), 'bankBytes': bank_path.stat().st_size,
            'bindings': bindings, 'runtimeValidated': False, 'nativeOverrideAvailable': False}


def verify(bank_path, mod_root, plan):
    if (not isinstance(plan, dict) or not isinstance(plan.get('bindings'), list)
            or type(plan.get('schema')) is not int or plan['schema'] != SCHEMA
            or plan.get('runtimeValidated') is not False or plan.get('nativeOverrideAvailable') is not False):
        raise ValueError('计划结构无效')
    request = {'schema': SCHEMA, 'replacements': [
        {'imageId': item['imageId'], 'png': item['png']} for item in plan['bindings']]}
    actual = prepare(bank_path, mod_root, request)
    if actual != plan:
        raise ValueError('计划已失效：资源文件、原生库或计划内容发生变化')
    return actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'verify'])
    parser.add_argument('game_directory', type=Path)
    parser.add_argument('mod_directory', type=Path)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        if file_hash(args.game_directory / 'Astral Ascent.exe') != bank.PROFILE_SHA:
            raise ValueError('不支持此 EXE 指纹')
        document = json.loads(args.input.read_text(encoding='utf-8-sig'))
        operation = prepare if args.action == 'prepare' else verify
        result = operation(args.game_directory / 'Assets.dat', args.mod_directory, document)
        text = json.dumps(result, ensure_ascii=False, indent=2)
        if args.output:
            destination = args.output.resolve()
            protected = [args.input.resolve(), (args.game_directory / 'Assets.dat').resolve(),
                         (args.game_directory / 'Astral Ascent.exe').resolve()]
            protected += [owned_file(args.mod_directory, item['png']) for item in
                          (document['replacements'] if args.action == 'prepare' else document['bindings'])]
            if destination in protected or destination.exists():
                raise ValueError('输出必须为新文件，不能覆盖输入或已有文件')
            with destination.open('x', encoding='utf-8') as stream:
                stream.write(text + '\n')
        else:
            print(text)
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(2, f'资源计划失败：{error}\n')


if __name__ == '__main__':
    main()
