#include "frontend_translator.h"
#include <initializer_list>
#include <utility>

namespace ananarchiver::qt {

FrontendTranslator::FrontendTranslator()
{
    const std::initializer_list<std::pair<const char*, const char*>> catalog {
        {"Open archive…", "打开压缩包…"},
        {"Open archive", "打开压缩包"},
        {"Close archive", "关闭压缩包"},
        {"Cancel", "取消"},
        {"File", "文件"},
        {"Quit", "退出"},
        {"Appearance", "外观"},
        {"Follow system", "跟随系统"},
        {"Light", "浅色"},
        {"Dark", "深色"},
        {"Help", "帮助"},
        {"About AnAnArchiver", "关于 AnAnArchiver"},
        {"All files (*)", "所有文件 (*)"},
        {"Archives (*.zip *.zipx *.7z *.rar *.tar *.gz *.tgz *.bz2 *.xz *.zst *.lz4 *.cab *.cpio *.ar *.iso *.lha *.lzh *.xar)",
         "压缩包 (*.zip *.zipx *.7z *.rar *.tar *.gz *.tgz *.bz2 *.xz *.zst *.lz4 *.cab *.cpio *.ar *.iso *.lha *.lzh *.xar)"},
        {"Browse folder", "浏览目录"},
        {"Browse folder…", "浏览目录…"},
        {"Home", "主页"},
        {"Current archive", "当前压缩包"},
        {"Compress", "压缩"},
        {"Extract", "解压"},
        {"Creating archives is not available in this version.", "当前版本尚不支持创建压缩包。"},
        {"Extraction is not available in this version.", "当前版本尚不支持解压。"},
        {"Folders", "目录"},
        {"Choose Browse folder to navigate local files.", "点击“浏览目录”查看本地文件。"},
        {"Name", "名称"},
        {"Size", "大小"},
        {"Type", "类型"},
        {"Modified", "修改时间"},
        {"Browse your archives", "浏览压缩包"},
        {"Open a ZIP, 7z, RAR, TAR or another supported archive to view its files.",
         "打开 ZIP、7z、RAR、TAR 等支持的压缩包，查看其中的文件。"},
        {"Unable to display archive contents", "无法显示压缩包内容"},
        {"No entries", "没有条目"},
        {"%1 entries", "%1 个条目"},
        {"%1 s", "%1 秒"},
        {"Version %1", "版本 %1"},
        {"A modern archive browser. Open an archive to inspect its contents.",
         "现代压缩包浏览器。打开压缩包，查看其中的内容。"},
        {"Unable to open archive", "无法打开压缩包"},
        {"Error details", "错误详情"},
        {"Unable to read folder", "无法读取目录"},
        {"Ready", "就绪"},
        {"Indexing archive…", "正在索引压缩包…"},
        {"Waiting for the previous operation to stop…", "正在等待上一个操作结束…"},
        {"Cancelling…", "正在取消…"},
        {"Cancelled — the list may be incomplete", "已取消，列表可能不完整"},
        {"Archive indexed", "压缩包索引完成"},
        {"Failed", "失败"},
        {"Failed — the list may be incomplete", "失败，列表可能不完整"},
        {"Folder", "目录"},
        {"Symbolic link", "符号链接"},
        {"Hard link", "硬链接"},
        {"Other", "其他"},
        {"Unknown", "未知"},
        {"The archive could not be found.", "找不到压缩包。"},
        {"Permission to read the archive was denied.", "没有读取压缩包的权限。"},
        {"This archive format is not supported.", "不支持此压缩包格式。"},
        {"This compression method is not supported.", "不支持此压缩方法。"},
        {"This operation is not supported.", "不支持此操作。"},
        {"The file is not a valid archive.", "此文件不是有效的压缩包。"},
        {"The archive is damaged or incomplete.", "压缩包已损坏或不完整。"},
        {"The archive password is incorrect.", "压缩包密码不正确。"},
        {"A password is required for this archive.", "此压缩包需要密码。"},
        {"There is not enough free disk space.", "磁盘空间不足。"},
        {"The output already exists.", "输出文件已存在。"},
        {"The operation was cancelled.", "操作已取消。"},
        {"The operation exceeded a safety limit.", "操作超出了安全限制。"},
        {"A filename contains invalid text encoding.", "文件名包含无效的文本编码。"},
        {"A file could not be read.", "无法读取文件。"},
        {"The archive could not be read reliably.", "无法可靠地读取压缩包。"},
        {"The archive operation failed.", "压缩包操作失败。"},
        {"Too many folders are being loaded. Please try expanding this folder again shortly.",
         "正在加载的目录过多，请稍后再次展开此目录。"},
        {"Archive to open", "要打开的压缩包"},
        {"Exit after this many milliseconds for launch verification", "在指定毫秒数后退出，用于启动验证"},
        {"Exit after archive indexing; failure returns exit code 1", "在压缩包索引完成后退出；失败时返回退出码 1"}
    };
    for (const auto& [source, translated] : catalog)
        translations_.insert(QString::fromUtf8(source), QString::fromUtf8(translated));
}

QString FrontendTranslator::translate(const char*, const char* sourceText, const char*, int) const
{
    const auto translated = translations_.constFind(QString::fromUtf8(sourceText));
    return translated == translations_.cend() ? QString() : *translated;
}

}
