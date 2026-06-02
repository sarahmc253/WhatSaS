with open(r'c:\Users\sreej\Code\WhatSaS\client-cpp\src\ui.hpp', 'r', encoding='utf-8') as f:
    content = f.read()

banner_start = content.find('R"(')
banner_end = content.find(')" C_MUTED', banner_start)

pre = content[:banner_start]
banner = content[banner_start:banner_end + 20]
post = content[banner_end + 20:]

OLD = '"            '
NEW = '"                '

pre2 = pre.replace(OLD, NEW)
post2 = post.replace(OLD, NEW)

result = pre2 + banner + post2

with open(r'c:\Users\sreej\Code\WhatSaS\client-cpp\src\ui.hpp', 'w', encoding='utf-8') as f:
    f.write(result)
print('done')
