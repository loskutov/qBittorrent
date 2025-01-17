/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2011  Vladimir Golovnev <glassez@yandex.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#include "powermanagement_x11.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingReply>

#include "base/global.h"

PowerManagementInhibitor::PowerManagementInhibitor(QObject *parent)
    : QObject(parent)
{
    if (!QDBusConnection::sessionBus().isConnected()) {
        qDebug("D-Bus: Could not connect to session bus");
        m_state = Error;
    }
    else {
        m_state = Idle;
    }

    m_intendedState = Idle;
    m_cookie = 0;
    m_useGSM = false;
}

void PowerManagementInhibitor::requestIdle()
{
    m_intendedState = Idle;
    if ((m_state == Error) || (m_state == Idle) || (m_state == RequestIdle) || (m_state == RequestBusy))
        return;

    m_state = RequestIdle;
    qDebug("D-Bus: PowerManagementInhibitor: Requesting idle");

    QDBusMessage call = m_useGSM
        ? QDBusMessage::createMethodCall(
            u"org.gnome.SessionManager"_qs,
            u"/org/gnome/SessionManager"_qs,
            u"org.gnome.SessionManager"_qs,
            u"Uninhibit"_qs)
        : QDBusMessage::createMethodCall(
            u"org.freedesktop.PowerManagement"_qs,
            u"/org/freedesktop/PowerManagement/Inhibit"_qs,
            u"org.freedesktop.PowerManagement.Inhibit"_qs,
            u"UnInhibit"_qs);
    call.setArguments({m_cookie});

    QDBusPendingCall pcall = QDBusConnection::sessionBus().asyncCall(call, 1000);
    auto *watcher = new QDBusPendingCallWatcher(pcall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &PowerManagementInhibitor::onAsyncReply);
}


void PowerManagementInhibitor::requestBusy()
{
    m_intendedState = Busy;
    if ((m_state == Error) || (m_state == Busy) || (m_state == RequestBusy) || (m_state == RequestIdle))
        return;

    m_state = RequestBusy;
    qDebug("D-Bus: PowerManagementInhibitor: Requesting busy");

    QDBusMessage call = m_useGSM
        ? QDBusMessage::createMethodCall(
            u"org.gnome.SessionManager"_qs,
            u"/org/gnome/SessionManager"_qs,
            u"org.gnome.SessionManager"_qs,
            u"Inhibit"_qs)
        : QDBusMessage::createMethodCall(
                u"org.freedesktop.PowerManagement"_qs,
                u"/org/freedesktop/PowerManagement/Inhibit"_qs,
                u"org.freedesktop.PowerManagement.Inhibit"_qs,
                u"Inhibit"_qs);

    QList<QVariant> args = {u"qBittorrent"_qs};
    if (m_useGSM)
        args << 0u;
    args << u"Active torrents are presented"_qs;
    if (m_useGSM)
        args << 8u;
    call.setArguments(args);

    QDBusPendingCall pcall = QDBusConnection::sessionBus().asyncCall(call, 1000);
    auto *watcher = new QDBusPendingCallWatcher(pcall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &PowerManagementInhibitor::onAsyncReply);
}

void PowerManagementInhibitor::onAsyncReply(QDBusPendingCallWatcher *call)
{
    if (m_state == RequestIdle) {
        QDBusPendingReply<> reply = *call;

        if (reply.isError()) {
            qDebug("D-Bus: Reply: Error: %s", qUtf8Printable(reply.error().message()));
            m_state = Error;
        }
        else {
            m_state = Idle;
            qDebug("D-Bus: PowerManagementInhibitor: Request successful");
            if (m_intendedState == Busy)
                requestBusy();
        }
    }
    else if (m_state == RequestBusy) {
        QDBusPendingReply<uint> reply = *call;

        if (reply.isError()) {
            qDebug("D-Bus: Reply: Error: %s", qUtf8Printable(reply.error().message()));

            if (!m_useGSM) {
                qDebug("D-Bus: Falling back to org.gnome.SessionManager");
                m_useGSM = true;
                m_state = Idle;
                if (m_intendedState == Busy)
                    requestBusy();
            }
            else {
                m_state = Error;
            }
        }
        else {
            m_state = Busy;
            m_cookie = reply.value();
            qDebug("D-Bus: PowerManagementInhibitor: Request successful, cookie is %d", m_cookie);
            if (m_intendedState == Idle)
                requestIdle();
        }
    }
    else {
        qDebug("D-Bus: Unexpected reply in state %d", m_state);
        m_state = Error;
    }

    call->deleteLater();
}
