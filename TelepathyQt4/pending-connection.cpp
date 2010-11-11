/*
 * This file is part of TelepathyQt4
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <TelepathyQt4/PendingConnection>

#include "TelepathyQt4/_gen/pending-connection.moc.hpp"

#include "TelepathyQt4/debug-internal.h"

#include <TelepathyQt4/ChannelFactory>
#include <TelepathyQt4/ConnectionManager>
#include <TelepathyQt4/Connection>
#include <TelepathyQt4/ContactFactory>
#include <TelepathyQt4/PendingReady>

#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace Tp
{

struct TELEPATHY_QT4_NO_EXPORT PendingConnection::Private
{
    ConnectionPtr connection;
};

/**
 * \class PendingConnection
 * \ingroup clientconn
 * \headerfile TelepathyQt4/pending-connection.h <TelepathyQt4/PendingConnection>
 *
 * \brief Class containing the parameters of and the reply to an asynchronous
 * connection request. Instances of this class cannot be constructed directly;
 * the only way to get one is via ConnectionManager.
 */

/**
 * Construct a PendingConnection object.
 *
 * \param manager ConnectionManager to use.
 * \param protocol Name of the protocol to create the connection for.
 * \param parameters Connection parameters.
 */
PendingConnection::PendingConnection(const ConnectionManagerPtr &manager,
        const QString &protocol, const QVariantMap &parameters)
    : PendingOperation(manager),
      mPriv(new Private)
{
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            manager->baseInterface()->RequestConnection(protocol,
                parameters), this);
    connect(watcher,
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(onCallFinished(QDBusPendingCallWatcher*)));
}

/**
 * Class destructor.
 */
PendingConnection::~PendingConnection()
{
    delete mPriv;
}

/**
 * Return the ConnectionManager object through which the request was made.
 *
 * \return Connection Manager object.
 */
ConnectionManagerPtr PendingConnection::manager() const
{
    return ConnectionManagerPtr(qobject_cast<ConnectionManager*>((ConnectionManager*) object().data()));
}

/**
 * Returns the newly created Connection object.
 *
 * \return Connection object.
 */
ConnectionPtr PendingConnection::connection() const
{
    if (!isFinished()) {
        warning() << "PendingConnection::connection called before finished, returning 0";
        return ConnectionPtr();
    } else if (!isValid()) {
        warning() << "PendingConnection::connection called when not valid, returning 0";
        return ConnectionPtr();
    }

    return mPriv->connection;
}

/**
 * Returns the connection's bus name ("service name"), or an empty string on
 * error.
 *
 * This method is useful for creating custom Connection objects: instead
 * of using PendingConnection::connection, one could construct a new custom
 * connection from the bus name and object path.
 *
 * \return Connection bus name
 * \sa objectPath()
 */
QString PendingConnection::busName() const
{
    if (!isFinished()) {
        warning() << "PendingConnection::busName called before finished";
    } else if (!isValid()) {
        warning() << "PendingConnection::busName called when not valid";
    }

    return mPriv->connection->busName();
}

/**
 * Returns the connection's object path or an empty string on error.
 *
 * This method is useful for creating custom Connection objects: instead
 * of using PendingConnection::connection, one could construct a new custom
 * connection with the bus name and object path.
 *
 * \return Connection object path
 * \sa busName()
 */
QString PendingConnection::objectPath() const
{
    if (!isFinished()) {
        warning() << "PendingConnection::connection called before finished";
    } else if (!isValid()) {
        warning() << "PendingConnection::connection called when not valid";
    }

    return mPriv->connection->objectPath();
}

void PendingConnection::onCallFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QString, QDBusObjectPath> reply = *watcher;

    if (!reply.isError()) {
        QString busName = reply.argumentAt<0>();
        QString objectPath = reply.argumentAt<1>().path();

        debug() << "Got reply to ConnectionManager.CreateConnection - bus name:" <<
            busName << "- object path:" << objectPath;

        PendingReady *readyOp = manager()->connectionFactory()->proxy(busName,
                objectPath, manager()->channelFactory(), manager()->contactFactory());
        mPriv->connection = ConnectionPtr::qObjectCast(readyOp->proxy());
        connect(readyOp,
                SIGNAL(finished(Tp::PendingOperation*)),
                SLOT(onConnectionBuilt(Tp::PendingOperation*)));
    } else {
        debug().nospace() <<
            "CreateConnection failed: " <<
            reply.error().name() << ": " << reply.error().message();
        setFinishedWithError(reply.error());
    }

    watcher->deleteLater();
}

void PendingConnection::onConnectionBuilt(Tp::PendingOperation *op)
{
    Q_ASSERT(op->isFinished());

    if (op->isError()) {
        warning() << "Making connection ready using the factory failed:" <<
            op->errorName() << op->errorMessage();
        setFinishedWithError(op->errorName(), op->errorMessage());
    } else {
        setFinished();
        debug() << "New connection" << mPriv->connection->objectPath() << "built";
    }
}

} // Tp
