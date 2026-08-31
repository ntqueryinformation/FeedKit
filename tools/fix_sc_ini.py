# Re-apply FeedKit ini fixes to Sven Co-op ReShade.ini after setup overwrite.
p = r'D:\SteamLibrary\steamapps\common\Sven Co-op\ReShade.ini'
data = open(p, 'rb').read().decode('utf-8')
BS = chr(92)

while (BS * 2 + '**') in data:
    data = data.replace(BS * 2 + '**', BS + '**')  # collapse **\** -> \**
data = data.replace('EffectSearchPaths=.', 'EffectSearchPaths=.' + BS) if False else data

import re
def set_key(text, key, value):
    pattern = re.compile(r'(?m)^(' + re.escape(key) + r'\s*=\s*).*$')
    if pattern.search(text):
        return pattern.sub(lambda m: m.group(1) + value, text, count=1), True
    return text, False

fixed = False
data, ch = set_key(data, 'EffectSearchPaths', '.{B}reshade-shaders{B}Shaders{B}**'.replace('{B}', chr(92)))
fixed |= ch
data, ch = set_key(data, 'TextureSearchPaths', '.{B}reshade-shaders{B}Textures{B}**'.replace('{B}', chr(92)))
fixed |= ch

# PreprocessorDefinitions: append DLSS5_MV_PROVIDER=3 if absent
if 'DLSS5_MV_PROVIDER' not in data:
    m = re.search(r'(?m)^PreprocessorDefinitions\s*=\s*(.*)$', data)
    if m:
        cur = m.group(1).strip()
        new = ('DLSS5_MV_PROVIDER=3,' + cur) if cur else 'DLSS5_MV_PROVIDER=3'
        data = data[:m.start(1)] + new + data[m.end(1):]
        fixed = True

open(p, 'wb').write(data.encode('utf-8'))
print('fixed:', fixed)
for line in data.splitlines():
    if 'SearchPaths' in line or 'PreprocessorDefinitions' in line:
        print(' ', line)
