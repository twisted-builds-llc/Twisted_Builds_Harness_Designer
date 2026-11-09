// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/ZipArchive.h"

#include <QCoreApplication>

#include <miniz.h>

namespace tbh::io {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::io::ZipArchive)
};
} // namespace

namespace {

// Refuse absurd entries (corrupt or malicious files) instead of allocating them.
constexpr mz_uint64 kMaxEntryBytes = 256ull * 1024 * 1024;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

std::optional<ZipEntries> readZip(const QByteArray &archive, QString *error)
{
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, archive.constData(), size_t(archive.size()), 0)) {
        setError(error, Tr::tr("The file is not a valid harness container (zip)."));
        return std::nullopt;
    }

    ZipEntries entries;
    std::optional<ZipEntries> result;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            setError(error, Tr::tr("The container is damaged (unreadable entry)."));
            mz_zip_reader_end(&zip);
            return std::nullopt;
        }
        if (stat.m_is_directory)
            continue;
        const QString name = QString::fromUtf8(stat.m_filename);
        if (stat.m_uncomp_size > kMaxEntryBytes) {
            setError(error, Tr::tr("The container entry \"%1\" is too large.").arg(name));
            mz_zip_reader_end(&zip);
            return std::nullopt;
        }
        size_t size = 0;
        void *data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data) {
            setError(error, Tr::tr("The container entry \"%1\" could not be decompressed.").arg(name));
            mz_zip_reader_end(&zip);
            return std::nullopt;
        }
        entries[name] = QByteArray(static_cast<const char *>(data), qsizetype(size));
        mz_free(data);
    }
    mz_zip_reader_end(&zip);
    return entries;
}

std::optional<QByteArray> writeZip(const ZipEntries &entries, QString *error)
{
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        setError(error, Tr::tr("Could not create the container in memory."));
        return std::nullopt;
    }
    for (const auto &[name, data] : entries) {
        const QByteArray utf8Name = name.toUtf8();
        if (!mz_zip_writer_add_mem(&zip, utf8Name.constData(), data.constData(), size_t(data.size()),
                                   MZ_DEFAULT_COMPRESSION)) {
            setError(error, Tr::tr("Could not add \"%1\" to the container.").arg(name));
            mz_zip_writer_end(&zip);
            return std::nullopt;
        }
    }
    void *buffer = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) {
        setError(error, Tr::tr("Could not finalize the container."));
        mz_zip_writer_end(&zip);
        return std::nullopt;
    }
    QByteArray archive(static_cast<const char *>(buffer), qsizetype(size));
    mz_free(buffer);
    mz_zip_writer_end(&zip);
    return archive;
}

} // namespace tbh::io
