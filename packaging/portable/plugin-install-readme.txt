OwO 插件安装说明
================

一、前提
--------
先安装 OwO 输入法本体（setup.exe 或便携版）。插件依赖本体自带的
owo_plugin_shell.exe 来安装。


二、安装
--------
1. 把本压缩包解压到任意目录（保持 install-plugin.bat 和 .owopkg 在一起）。
2. 双击 install-plugin.bat。
3. 脚本会自动：
   - 找到本体的 owo_plugin_shell.exe
   - 把插件装到 %LOCALAPPDATA%\OwO\InputMethod\plugins（后台服务默认加载目录）
   - 激活插件
4. 装完重启 OwO 后台服务（或重新登录Windows），已打开的程序需重启。


三、关于「接受风险」提示
------------------------
本插件由开发者证书签名，不是受信 CA 证书，所以安装时会以「接受风险」方式
进行（类似手机安装未知来源应用）。这是设计使然：受信发布者可静默安装，
其他来源需要你明确确认。若不放心，可对照仓库源码自行编译与校验。


四、卸载
--------
在本体bin 目录下用命令行：
  owo_plugin_shell.exe "%LOCALAPPDATA%\OwO\InputMethod\plugins" uninstall <插件ID> <版本>
例如：
  owo_plugin_shell.exe "%LOCALAPPDATA%\OwO\InputMethod\plugins" uninstall owo.plugin.emoji 0.3.0


五、说明
--------
表情/符号面板功能其实已内置在输入法本体中，本插件主要用于演示插件系统的
下载与安装流程是否通畅。
