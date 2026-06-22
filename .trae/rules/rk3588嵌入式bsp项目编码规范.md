---
# 仅匹配项目代码文件，doc/fonts目录不触发代码约束，节省Token
paths:
- *.c
- *.cpp
- *.h
- *.hpp
- display.sh
---
# 嵌入式跑马灯（EGL/GLES/FreeType）项目强制规则
## 1. 代码规范
1. C/C++代码使用4空格缩进，变量小驼峰、常量全大写、类名大驼峰
2. 头文件使用头文件保护宏，避免重复包含
3. OpenGL/EGL/GLES相关接口必须补充注释说明渲染流程
4. FreeType字体、图片加载逻辑标注资源路径适配逻辑
5. JSON解析基于json.hpp，统一数据结构体读写规范

## 2. 目录过滤（免费版减少扫描消耗）
include:
- ./*.c
- ./*.cpp
- ./*.h
- ./*.hpp
exclude:
- doc/        # 需求文档不参与代码分析
- fonts/      # 字体文件不读取、不解析二进制ttf
- test/       # 测试目录默认忽略，需要分析再手动指定

## 3. Git提交规范（强制）
1. GitHub用户名: lerbao
2. 远程仓库: git@github.com:lerbao/Marquee.git (SSH方式)
3. SSH密钥路径: C:\Users\20808\.ssh\id_ed25519
4. 禁止使用LFS！二进制文件(.ttc/.ttf/.so)直接提交，不创建.gitattributes
5. 提交前必须执行 git status 检查，确认无遗漏文件
6. 推送命令固定: git push origin main
7. 提交信息格式: 中文简要描述，如"修复xxx问题"、"新增xxx功能"
8. 禁止提交编译产物(.o/.obj)、IDE临时文件、.history目录

## 4. 输出约束
1. 修改代码优先输出变更片段，完整文件按需提供
2. shell脚本display.sh修改附带可直接运行的调试命令
3. 新增功能贴合现有文件结构，不随意新建目录
4. 编译、运行调试命令单独代码块，复制即用
5. 不读取doc下PRD/架构文档，除非我明确要求参考
6. 严格自己检查好逻辑问题，不要因为逻辑问题反复修改代码