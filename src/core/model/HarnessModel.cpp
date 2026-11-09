// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/HarnessModel.h"

#include "model/Library.h"

#include <algorithm>

namespace tbh {


template<>
std::vector<Connector> &HarnessModel::items<Connector>() { return m_harness.connectors; }
template<>
std::vector<Wire> &HarnessModel::items<Wire>() { return m_harness.wires; }
template<>
std::vector<Splice> &HarnessModel::items<Splice>() { return m_harness.splices; }
template<>
std::vector<LayoutNode> &HarnessModel::items<LayoutNode>() { return m_harness.layoutNodes; }
template<>
std::vector<LayoutSegment> &HarnessModel::items<LayoutSegment>() { return m_harness.layoutSegments; }
template<>
const std::vector<LayoutNode> &HarnessModel::items<LayoutNode>() const { return m_harness.layoutNodes; }
template<>
const std::vector<LayoutSegment> &HarnessModel::items<LayoutSegment>() const { return m_harness.layoutSegments; }
template<>
const std::vector<Connector> &HarnessModel::items<Connector>() const { return m_harness.connectors; }
template<>
const std::vector<Wire> &HarnessModel::items<Wire>() const { return m_harness.wires; }
template<>
const std::vector<Splice> &HarnessModel::items<Splice>() const { return m_harness.splices; }

HarnessModel::HarnessModel(QObject *parent)
    : QObject(parent)
{
}

void HarnessModel::reset(Harness harness)
{
    m_harness = std::move(harness);
    m_library = mergeLibraries(m_userLibrary, m_harness.library);
    emit harnessReset();
}

void HarnessModel::setUserLibrary(Library library)
{
    m_userLibrary = std::move(library);
    m_library = mergeLibraries(m_userLibrary, m_harness.library);
    emit libraryChanged();
}

void HarnessModel::setInfo(const HarnessInfo &info)
{
    if (m_harness.info == info)
        return;
    m_harness.info = info;
    emit infoChanged();
}

void HarnessModel::setSettings(const Settings &settings)
{
    if (m_harness.settings == settings)
        return;
    m_harness.settings = settings;
    emit settingsChanged();
}

template<typename T>
const T *HarnessModel::find(const QString &id) const
{
    const auto &list = items<T>();
    const auto it = std::find_if(list.begin(), list.end(), [&](const T &item) { return item.id == id; });
    return it == list.end() ? nullptr : &*it;
}

template<typename T>
qsizetype HarnessModel::indexOf(const QString &id) const
{
    const auto &list = items<T>();
    const auto it = std::find_if(list.begin(), list.end(), [&](const T &item) { return item.id == id; });
    return it == list.end() ? -1 : qsizetype(it - list.begin());
}

template<typename T>
void HarnessModel::insert(T item, qsizetype index)
{
    auto &list = items<T>();
    if (index < 0 || index > qsizetype(list.size()))
        index = qsizetype(list.size());
    const QString id = item.id;
    list.insert(list.begin() + index, std::move(item));
    emit entityAdded(entityKindOf<T>(), id);
}

template<typename T>
void HarnessModel::update(T item)
{
    const qsizetype index = indexOf<T>(item.id);
    Q_ASSERT(index >= 0);
    if (index < 0)
        return;
    auto &slot = items<T>()[size_t(index)];
    if (slot == item)
        return;
    slot = std::move(item);
    emit entityChanged(entityKindOf<T>(), slot.id);
}

template<typename T>
void HarnessModel::remove(const QString &id)
{
    const qsizetype index = indexOf<T>(id);
    Q_ASSERT(index >= 0);
    if (index < 0)
        return;
    auto &list = items<T>();
    list.erase(list.begin() + index);
    for (const QString &key : placementKeys(entityKindOf<T>(), id))
        m_harness.schematic.placements.erase(key);
    emit entityRemoved(entityKindOf<T>(), id);
}

std::vector<QString> HarnessModel::placementKeys(EntityKind kind, const QString &id)
{
    switch (kind) {
    case EntityKind::Wire:
        return {freeEndPlacementKey(id, false), freeEndPlacementKey(id, true)};
    case EntityKind::Connector:
    case EntityKind::Splice:
        return {id};
    case EntityKind::LayoutNode:
    case EntityKind::LayoutSegment:
        return {};
    }
    return {};
}

const SchematicPlacement *HarnessModel::placement(const QString &key) const
{
    const auto &placements = m_harness.schematic.placements;
    const auto it = placements.find(key);
    return it == placements.end() ? nullptr : &it->second;
}

void HarnessModel::setPlacement(const QString &key, const std::optional<SchematicPlacement> &placement)
{
    auto &placements = m_harness.schematic.placements;
    const auto it = placements.find(key);
    if (!placement) {
        if (it == placements.end())
            return;
        placements.erase(it);
    } else {
        if (it != placements.end() && it->second == *placement)
            return;
        placements[key] = *placement;
    }
    emit placementChanged(key);
}

#define TBH_INSTANTIATE(T)                                                  \
    template const T *HarnessModel::find<T>(const QString &) const;         \
    template qsizetype HarnessModel::indexOf<T>(const QString &) const;     \
    template void HarnessModel::insert<T>(T, qsizetype);                    \
    template void HarnessModel::update<T>(T);                               \
    template void HarnessModel::remove<T>(const QString &);
TBH_INSTANTIATE(Connector)
TBH_INSTANTIATE(Wire)
TBH_INSTANTIATE(Splice)
TBH_INSTANTIATE(LayoutNode)
TBH_INSTANTIATE(LayoutSegment)
#undef TBH_INSTANTIATE

} // namespace tbh
