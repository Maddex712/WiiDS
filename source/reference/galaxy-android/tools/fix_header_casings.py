import os

# 1. Rename FIleSelectItem.hpp -> FileSelectItem.hpp
file_select = 'decomp/include/Game/Map/FIleSelectItem.hpp'
target_file_select = 'decomp/include/Game/Map/FileSelectItem.hpp'
if os.path.exists(file_select):
    os.system(f'git mv {file_select} decomp/include/Game/Map/temp.hpp && git mv decomp/include/Game/Map/temp.hpp {target_file_select}')
    print("Renamed FIleSelectItem.hpp -> FileSelectItem.hpp")

# 2. Rename KarikariDirector.hpp -> KariKariDirector.hpp
karikari = 'decomp/include/Game/Enemy/KarikariDirector.hpp'
target_karikari = 'decomp/include/Game/Enemy/KariKariDirector.hpp'
if os.path.exists(karikari):
    os.system(f'git mv {karikari} decomp/include/Game/Enemy/temp.hpp && git mv decomp/include/Game/Enemy/temp.hpp {target_karikari}')
    print("Renamed KarikariDirector.hpp -> KariKariDirector.hpp")

# Also add KarikariDirector.hpp redirect
with open('decomp/include/Game/Enemy/KarikariDirector.hpp', 'w') as f:
    f.write('#pragma once\n#include "KariKariDirector.hpp"\n')

# 3. Create uppercase redirect headers in decomp/libs/RVL_SDK/include/revolution/
rev_dir = 'decomp/libs/RVL_SDK/include/revolution'
redirects = {
    'MEM.h': 'mem.h',
    'OS.h': 'os.h',
    'FS.h': 'fs.h',
    'NAND.h': 'nand.h',
    'BTE.h': 'bte.h',
    'SC.h': 'sc.h',
    'DVD.h': 'dvd.h',
}
for up, low in redirects.items():
    p = os.path.join(rev_dir, up)
    with open(p, 'w') as f:
        f.write(f'#pragma once\n#include "{low}"\n')
    print(f"Created redirect {up} -> {low}")

# 4. In revolution/os/, add OSTIme.h -> OSTime.h
with open(os.path.join(rev_dir, 'os', 'OSTIme.h'), 'w') as f:
    f.write('#pragma once\n#include "OSTime.h"\n')
print("Created redirect OSTIme.h -> OSTime.h")

# 5. In Game/Animation/, add Xanimecore.hpp -> XanimeCore.hpp
with open('decomp/include/Game/Animation/Xanimecore.hpp', 'w') as f:
    f.write('#pragma once\n#include "XanimeCore.hpp"\n')
print("Created redirect Xanimecore.hpp -> XanimeCore.hpp")

print("All header casing fixes applied successfully!")
