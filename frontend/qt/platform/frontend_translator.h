#pragma once

#include <QHash>
#include <QTranslator>

namespace ananarchiver::qt {

// A small source-backed catalog keeps the bootstrap reproducible without
// requiring the optional Qt Linguist tools at build time.
class FrontendTranslator final : public QTranslator
{
public:
    FrontendTranslator();
    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation = nullptr, int n = -1) const override;
    bool isEmpty() const override { return translations_.isEmpty(); }

private:
    QHash<QString, QString> translations_;
};

}
