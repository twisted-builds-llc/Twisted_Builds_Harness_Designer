// SPDX-License-Identifier: GPL-3.0-or-later
#include "Translations.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace tbh::app {

void installTranslators(QCoreApplication &app)
{
    auto *qt = new QTranslator(&app);
    if (qt->load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                 QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(qt);
    else
        delete qt;

    auto *own = new QTranslator(&app);
    const QString base = QStringLiteral("twisted-harness-designer");
    if (own->load(QLocale(), base, QStringLiteral("_"), QStringLiteral(":/i18n"))
        || own->load(base + QStringLiteral("_en"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(own);
    else
        delete own;
}

} // namespace tbh::app
