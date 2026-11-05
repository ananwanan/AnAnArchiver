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
      'e9': '此归档需要密码，密码输入计划在 F4 接入。',
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
      'e9': '此归档需要密码，密码输入计划在 F4 接入。',
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
}
