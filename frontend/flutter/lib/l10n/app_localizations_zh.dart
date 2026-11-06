// ignore: unused_import
import 'package:intl/intl.dart' as intl;

import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for Chinese (`zh`).
class AppLocalizationsZh extends AppLocalizations {
  AppLocalizationsZh([String locale = 'zh']) : super(locale);

  @override
  String get appTitle => '轻匣';

  @override
  String get home => '主页';

  @override
  String get openArchive => '打开归档';

  @override
  String get closeArchive => '关闭归档';

  @override
  String get homeTitle => '归档文件，清晰有序。';

  @override
  String get homeBody => '打开一个归档，浏览其中的文件夹与文件。';

  @override
  String get archiveRoot => '归档根目录';

  @override
  String get up => '返回上级';

  @override
  String get name => '名称';

  @override
  String get size => '大小';

  @override
  String get type => '类型';

  @override
  String get modified => '修改时间';

  @override
  String get search => '搜索当前文件夹';

  @override
  String get empty => '此文件夹中没有项目';

  @override
  String get selectAll => '全选当前查询';

  @override
  String get cancel => '取消';

  @override
  String get folders => '文件夹';

  @override
  String get more => '下一页文件夹';

  @override
  String get previous => '上一页文件夹';

  @override
  String get language => '语言';

  @override
  String get theme => '主题';

  @override
  String get system => '跟随系统';

  @override
  String get light => '浅色';

  @override
  String get dark => '深色';

  @override
  String get errorTitle => '无法完成请求';

  @override
  String get details => '详情';

  @override
  String get retry => '重试';

  @override
  String get loading => '正在加载';

  @override
  String get file => '文件';

  @override
  String get folder => '文件夹';

  @override
  String get link => '链接';

  @override
  String get other => '其他';

  @override
  String get close => '关闭';

  @override
  String get selectionLimit => '选择区间过多，请使用连续选择或全选。';

  @override
  String status(int count, int selected) {
    return '$count 个项目 · 已选择 $selected 个';
  }

  @override
  String task(String operation, String state, int entries, int ms) {
    return '$operation · $state · $entries 个条目 · $ms 毫秒';
  }

  @override
  String operation(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'open': '打开归档',
      'query': '查询目录',
      'page': '加载文件',
      'folders': '加载文件夹',
      'selection': '解析选择',
      'extractAll': '解压全部',
      'extractSelected': '解压选中',
      'compress': '创建归档',
      'export': '准备拖出文件',
      'probe': '检测拖入文件',
      'other': '归档任务',
    });
    return '$_temp0';
  }

  @override
  String taskState(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      's1': '等待中',
      's2': '进行中',
      's3': '正在取消',
      's4': '已取消',
      's5': '已完成',
      's6': '失败',
      'other': '未知',
    });
    return '$_temp0';
  }

  @override
  String errorMessage(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'e1': '找不到文件。',
      'e2': '没有访问权限。',
      'e3': '不支持的归档格式。',
      'e4': '不支持的压缩方法。',
      'e5': '不支持此操作。',
      'e6': '归档无效。',
      'e7': '归档已损坏。',
      'e8': '密码错误。',
      'e9': '此归档需要密码。',
      'e10': '磁盘空间不足。',
      'e11': '输出已存在。',
      'e12': '操作已取消。',
      'e13': '清理失败。',
      'e14': '已达到资源上限。',
      'e15': '路径编码无效。',
      'e16': '归档路径不安全。',
      'e17': '发生输入输出错误。',
      'e18': '归档后端报告警告。',
      'e19': '归档后端报告错误。',
      'other': '请求失败，请查看详情。',
    });
    return '$_temp0';
  }

  @override
  String get components => '开源组件与构建';

  @override
  String get licenses => '许可证';

  @override
  String get unavailable => '不可用';

  @override
  String get modifiedBuild => '含未提交修改';

  @override
  String get createArchive => '创建归档';

  @override
  String get extractAll => '解压全部';

  @override
  String get extractSelected => '解压选中';

  @override
  String get extract => '解压';

  @override
  String get password => '密码';

  @override
  String get overwrite => '目标文件已存在时';

  @override
  String get applyToAll => '应用于本任务全部冲突';

  @override
  String get conflictTitle => '目标文件已存在';

  @override
  String get staleAnswer => '此冲突已不再等待回答。';

  @override
  String get cleanupNotice => '操作未完成。解压会回滚新建输出，请检查目标目录及清理错误详情；已存在的文件可能已被替换。';

  @override
  String get addFiles => '添加文件';

  @override
  String get addFolder => '添加文件夹';

  @override
  String get clearInputs => '清空输入';

  @override
  String get inputLimit => '最多允许 4,096 个输入路径。';

  @override
  String inputCount(int count) {
    return '$count 个输入路径';
  }

  @override
  String get formatMethod => '格式 / 方法';

  @override
  String get level => '压缩等级';

  @override
  String get preservePermissions => '保留权限';

  @override
  String get preserveTimestamps => '保留时间戳';

  @override
  String get chooseOutput => '选择输出位置';

  @override
  String get replaceArchive => '创建成功后替换已有归档';

  @override
  String overwritePolicy(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'p0': '询问',
      'p1': '覆盖',
      'p2': '跳过',
      'p3': '重命名',
      'p4': '仅覆盖较旧文件',
      'other': '未知',
    });
    return '$_temp0';
  }

  @override
  String operationStats(String entries, String bytes, String speed, int ms) {
    return '$entries 个条目 · $bytes 字节 · $speed 字节/秒 · $ms 毫秒';
  }

  @override
  String compressionPhase(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'p1': '扫描输入',
      'p2': '写入归档',
      'p3': '完成输出',
      'other': '处理中',
    });
    return '$_temp0';
  }

  @override
  String get taskCenter => '任务中心';

  @override
  String get noTasks => '本次会话尚无任务。';

  @override
  String get clearHistory => '清空记录';

  @override
  String get recentArchives => '最近归档';

  @override
  String get removeRecent => '移除此记录';

  @override
  String get settingsProblem => '设置读取或保存失败，原数据已保留。请查看详情。';

  @override
  String get prepareDrag => '准备选中项以拖出';

  @override
  String get dragReady => '拖动已准备的文件';

  @override
  String get exportFiles => '临时导出';

  @override
  String get exportRetention =>
      '目标程序可能在拖放结束或轻匣退出后继续读取。交给系统的文件会跨启动保留在下列私有目录。请在所有目标复制完成后释放。最多四份导出，每份 512 MiB；准备后尚未交给系统的输出十分钟后过期。';

  @override
  String get releaseExports => '所有目标复制已完成：释放文件';
}

/// The translations for Chinese, as used in China (`zh_CN`).
class AppLocalizationsZhCn extends AppLocalizationsZh {
  AppLocalizationsZhCn() : super('zh_CN');

  @override
  String get appTitle => '轻匣';

  @override
  String get home => '主页';

  @override
  String get openArchive => '打开归档';

  @override
  String get closeArchive => '关闭归档';

  @override
  String get homeTitle => '归档文件，清晰有序。';

  @override
  String get homeBody => '打开一个归档，浏览其中的文件夹与文件。';

  @override
  String get archiveRoot => '归档根目录';

  @override
  String get up => '返回上级';

  @override
  String get name => '名称';

  @override
  String get size => '大小';

  @override
  String get type => '类型';

  @override
  String get modified => '修改时间';

  @override
  String get search => '搜索当前文件夹';

  @override
  String get empty => '此文件夹中没有项目';

  @override
  String get selectAll => '全选当前查询';

  @override
  String get cancel => '取消';

  @override
  String get folders => '文件夹';

  @override
  String get more => '下一页文件夹';

  @override
  String get previous => '上一页文件夹';

  @override
  String get language => '语言';

  @override
  String get theme => '主题';

  @override
  String get system => '跟随系统';

  @override
  String get light => '浅色';

  @override
  String get dark => '深色';

  @override
  String get errorTitle => '无法完成请求';

  @override
  String get details => '详情';

  @override
  String get retry => '重试';

  @override
  String get loading => '正在加载';

  @override
  String get file => '文件';

  @override
  String get folder => '文件夹';

  @override
  String get link => '链接';

  @override
  String get other => '其他';

  @override
  String get close => '关闭';

  @override
  String get selectionLimit => '选择区间过多，请使用连续选择或全选。';

  @override
  String status(int count, int selected) {
    return '$count 个项目 · 已选择 $selected 个';
  }

  @override
  String task(String operation, String state, int entries, int ms) {
    return '$operation · $state · $entries 个条目 · $ms 毫秒';
  }

  @override
  String operation(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'open': '打开归档',
      'query': '查询目录',
      'page': '加载文件',
      'folders': '加载文件夹',
      'selection': '解析选择',
      'extractAll': '解压全部',
      'extractSelected': '解压选中',
      'compress': '创建归档',
      'export': '准备拖出文件',
      'probe': '检测拖入文件',
      'other': '归档任务',
    });
    return '$_temp0';
  }

  @override
  String taskState(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      's1': '等待中',
      's2': '进行中',
      's3': '正在取消',
      's4': '已取消',
      's5': '已完成',
      's6': '失败',
      'other': '未知',
    });
    return '$_temp0';
  }

  @override
  String errorMessage(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'e1': '找不到文件。',
      'e2': '没有访问权限。',
      'e3': '不支持的归档格式。',
      'e4': '不支持的压缩方法。',
      'e5': '不支持此操作。',
      'e6': '归档无效。',
      'e7': '归档已损坏。',
      'e8': '密码错误。',
      'e9': '此归档需要密码。',
      'e10': '磁盘空间不足。',
      'e11': '输出已存在。',
      'e12': '操作已取消。',
      'e13': '清理失败。',
      'e14': '已达到资源上限。',
      'e15': '路径编码无效。',
      'e16': '归档路径不安全。',
      'e17': '发生输入输出错误。',
      'e18': '归档后端报告警告。',
      'e19': '归档后端报告错误。',
      'other': '请求失败，请查看详情。',
    });
    return '$_temp0';
  }

  @override
  String get components => '开源组件与构建';

  @override
  String get licenses => '许可证';

  @override
  String get unavailable => '不可用';

  @override
  String get modifiedBuild => '含未提交修改';

  @override
  String get createArchive => '创建归档';

  @override
  String get extractAll => '解压全部';

  @override
  String get extractSelected => '解压选中';

  @override
  String get extract => '解压';

  @override
  String get password => '密码';

  @override
  String get overwrite => '目标文件已存在时';

  @override
  String get applyToAll => '应用于本任务全部冲突';

  @override
  String get conflictTitle => '目标文件已存在';

  @override
  String get staleAnswer => '此冲突已不再等待回答。';

  @override
  String get cleanupNotice => '操作未完成。解压会回滚新建输出，请检查目标目录及清理错误详情；已存在的文件可能已被替换。';

  @override
  String get addFiles => '添加文件';

  @override
  String get addFolder => '添加文件夹';

  @override
  String get clearInputs => '清空输入';

  @override
  String get inputLimit => '最多允许 4,096 个输入路径。';

  @override
  String inputCount(int count) {
    return '$count 个输入路径';
  }

  @override
  String get formatMethod => '格式 / 方法';

  @override
  String get level => '压缩等级';

  @override
  String get preservePermissions => '保留权限';

  @override
  String get preserveTimestamps => '保留时间戳';

  @override
  String get chooseOutput => '选择输出位置';

  @override
  String get replaceArchive => '创建成功后替换已有归档';

  @override
  String overwritePolicy(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'p0': '询问',
      'p1': '覆盖',
      'p2': '跳过',
      'p3': '重命名',
      'p4': '仅覆盖较旧文件',
      'other': '未知',
    });
    return '$_temp0';
  }

  @override
  String operationStats(String entries, String bytes, String speed, int ms) {
    return '$entries 个条目 · $bytes 字节 · $speed 字节/秒 · $ms 毫秒';
  }

  @override
  String compressionPhase(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'p1': '扫描输入',
      'p2': '写入归档',
      'p3': '完成输出',
      'other': '处理中',
    });
    return '$_temp0';
  }

  @override
  String get taskCenter => '任务中心';

  @override
  String get noTasks => '本次会话尚无任务。';

  @override
  String get clearHistory => '清空记录';

  @override
  String get recentArchives => '最近归档';

  @override
  String get removeRecent => '移除此记录';

  @override
  String get settingsProblem => '设置读取或保存失败，原数据已保留。请查看详情。';

  @override
  String get prepareDrag => '准备选中项以拖出';

  @override
  String get dragReady => '拖动已准备的文件';

  @override
  String get exportFiles => '临时导出';

  @override
  String get exportRetention =>
      '目标程序可能在拖放结束或轻匣退出后继续读取。交给系统的文件会跨启动保留在下列私有目录。请在所有目标复制完成后释放。最多四份导出，每份 512 MiB；准备后尚未交给系统的输出十分钟后过期。';

  @override
  String get releaseExports => '所有目标复制已完成：释放文件';
}
