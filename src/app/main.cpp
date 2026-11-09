// SPDX-License-Identifier: GPL-3.0-or-later
#include "Translations.h"
#include "ui/MainWindow.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Twisted Builds"));
    QApplication::setApplicationName(QStringLiteral("Harness Designer"));
    QApplication::setApplicationDisplayName(QStringLiteral("Twisted Builds Harness Designer"));
    QApplication::setApplicationVersion(QStringLiteral(TBH_APP_VERSION));
    tbh::app::installTranslators(app);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QApplication::translate("main", "Harness file to open."));
    parser.process(app);

    tbh::app::Theme theme;
    tbh::app::MainWindow window(&theme);
    window.show();
    if (!parser.positionalArguments().isEmpty())
        window.openFile(parser.positionalArguments().constFirst());
    return app.exec();
}
