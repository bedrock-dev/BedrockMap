这边文档用于介绍qt的i18n的key的命名规则
所有的key分如下类别：

第一类：UI文件中，直接使用直接使用key即可，key的名字就是widget/window的名字+.+实际内容，其中widget的第一个字母大写，比如mainWindow.xxx, mapWidget.xxx(此类无需修改)

对于第二类，包含cpp中的窗口标题，右键菜单的名字，menu的名字，窗口标题,还有打开/保存/选择文件的dialog的标题，等，直接把在cpp里面用tr("widget.domain.content")的形式，其实widget是所在widget的名字，首字母小写，如mapWidget,mainWindow等，domain是域名，包括rightMenu,menu,title,fileDialog这四种，分别对应上面说的四种，cotent就是实际内容。注意：文件dialog中的接收文件的描述符不需要做i18n，保留AllFiles这种描述。

第三类，包含所有的提示信息，句子，统一调用msg::SENTNECCE()这种格式，里面的key就是msg.xxxx,xxx是英文的句子简短描述，注意，所有的弹窗统一使用msg.h中的INFO,和WARN函数，不要用QMessageBox::这种裸露接口。
