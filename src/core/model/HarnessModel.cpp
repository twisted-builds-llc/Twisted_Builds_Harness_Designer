// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/HarnessModel.h"

namespace tbh {

HarnessModel::HarnessModel(QObject *parent)
    : QObject(parent)
{
}

void HarnessModel::reset(Harness harness)
{
    m_harness = std::move(harness);
    emit harnessReset();
}

void HarnessModel::setInfo(const HarnessInfo &info)
{
    if (m_harness.info == info)
        return;
    m_harness.info = info;
    emit infoChanged();
}

} // namespace tbh
