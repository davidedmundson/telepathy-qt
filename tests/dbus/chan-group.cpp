#include <QtCore/QDebug>
#include <QtCore/QTimer>

#include <QtDBus/QtDBus>

#include <QtTest/QtTest>

#include <TelepathyQt4/Client/Channel>
#include <TelepathyQt4/Client/Connection>
#include <TelepathyQt4/Client/ContactManager>
#include <TelepathyQt4/Client/PendingChannel>
#include <TelepathyQt4/Client/PendingContacts>
#include <TelepathyQt4/Client/PendingHandles>
#include <TelepathyQt4/Client/ReferencedHandles>
#include <TelepathyQt4/Debug>

#include <telepathy-glib/debug.h>

#include <tests/lib/csh/conn.h>
#include <tests/lib/test.h>

using namespace Telepathy::Client;

class TestChanGroup : public Test
{
    Q_OBJECT

public:
    TestChanGroup(QObject *parent = 0)
        : Test(parent), mConnService(0), mConn(0), mChan(0)
    { }

protected Q_SLOTS:
    void expectConnReady(uint, uint);
    void expectConnInvalidated();
    void expectPendingRoomHandlesFinished(Telepathy::Client::PendingOperation*);
    void expectPendingContactHandlesFinished(Telepathy::Client::PendingOperation*);
    void expectCreateChannelFinished(Telepathy::Client::PendingOperation *);
    void expectPendingContactsFinished(Telepathy::Client::PendingOperation *);
    void onChannelGroupFlagsChanged(uint, uint, uint);
    void onGroupMembersChanged(
            const QList<QSharedPointer<Contact> > &groupMembersAdded,
            const QList<QSharedPointer<Contact> > &groupLocalPendingMembersAdded,
            const QList<QSharedPointer<Contact> > &groupRemotePendingMembersAdded,
            const QList<QSharedPointer<Contact> > &groupMembersRemoved,
            const Channel::GroupMemberChangeDetails &details);

private Q_SLOTS:
    void initTestCase();
    void init();

    void testRequestHandle();
    void testCreateChannel();
    void testCreateChannelDetailed();

    void cleanup();
    void cleanupTestCase();

private:
    void debugContacts();
    void doTestCreateChannel();

    QString mConnName, mConnPath;
    ExampleCSHConnection *mConnService;
    Connection *mConn;
    Channel *mChan;
    QString mChanObjectPath;
    uint mRoomNumber;
    ReferencedHandles mRoomHandles;
    ReferencedHandles mContactHandles;
    QList<QSharedPointer<Contact> > mContacts;
};

void TestChanGroup::expectConnReady(uint newStatus, uint newStatusReason)
{
    qDebug() << "connection changed to status" << newStatus;
    switch (newStatus) {
    case Connection::StatusDisconnected:
        qWarning() << "Disconnected";
        mLoop->exit(1);
        break;
    case Connection::StatusConnecting:
        /* do nothing */
        break;
    case Connection::StatusConnected:
        qDebug() << "Ready";
        mLoop->exit(0);
        break;
    default:
        qWarning().nospace() << "What sort of status is "
            << newStatus << "?!";
        mLoop->exit(2);
        break;
    }
}

void TestChanGroup::expectConnInvalidated()
{
    mLoop->exit(0);
}

void TestChanGroup::expectPendingRoomHandlesFinished(PendingOperation *op)
{
    if (!op->isFinished()) {
        qWarning() << "unfinished";
        mLoop->exit(1);
        return;
    }

    if (op->isError()) {
        qWarning().nospace() << op->errorName()
            << ": " << op->errorMessage();
        mLoop->exit(2);
        return;
    }

    if (!op->isValid()) {
        qWarning() << "inconsistent results";
        mLoop->exit(3);
        return;
    }

    qDebug() << "finished";
    PendingHandles *pending = qobject_cast<PendingHandles*>(op);
    mRoomHandles = pending->handles();
    mLoop->exit(0);
}

void TestChanGroup::expectPendingContactHandlesFinished(PendingOperation *op)
{
    if (!op->isFinished()) {
        qWarning() << "unfinished";
        mLoop->exit(1);
        return;
    }

    if (op->isError()) {
        qWarning().nospace() << op->errorName()
            << ": " << op->errorMessage();
        mLoop->exit(2);
        return;
    }

    if (!op->isValid()) {
        qWarning() << "inconsistent results";
        mLoop->exit(3);
        return;
    }

    qDebug() << "finished";
    PendingHandles *pending = qobject_cast<PendingHandles*>(op);
    mContactHandles = pending->handles();
    mLoop->exit(0);
}

void TestChanGroup::expectCreateChannelFinished(PendingOperation* op)
{
    if (!op->isFinished()) {
        qWarning() << "unfinished";
        mLoop->exit(1);
        return;
    }

    if (op->isError()) {
        qWarning().nospace() << op->errorName()
            << ": " << op->errorMessage();
        mLoop->exit(2);
        return;
    }

    if (!op->isValid()) {
        qWarning() << "inconsistent results";
        mLoop->exit(3);
        return;
    }

    PendingChannel *pc = qobject_cast<PendingChannel*>(op);
    mChan = pc->channel();
    mChanObjectPath = mChan->objectPath();
    mLoop->exit(0);
}

void TestChanGroup::expectPendingContactsFinished(PendingOperation *op)
{
    if (!op->isFinished()) {
        qWarning() << "unfinished";
        mLoop->exit(1);
        return;
    }

    if (op->isError()) {
        qWarning().nospace() << op->errorName()
            << ": " << op->errorMessage();
        mLoop->exit(2);
        return;
    }

    if (!op->isValid()) {
        qWarning() << "inconsistent results";
        mLoop->exit(3);
        return;
    }

    qDebug() << "finished";
    PendingContacts *pending = qobject_cast<PendingContacts *>(op);
    mContacts = pending->contacts();

    mLoop->exit(0);
}

void TestChanGroup::onChannelGroupFlagsChanged(uint flags, uint added, uint removed)
{
    qDebug() << "group flags changed";
    mLoop->exit(0);
}

void TestChanGroup::onGroupMembersChanged(
        const QList<QSharedPointer<Contact> > &groupMembersAdded,
        const QList<QSharedPointer<Contact> > &groupLocalPendingMembersAdded,
        const QList<QSharedPointer<Contact> > &groupRemotePendingMembersAdded,
        const QList<QSharedPointer<Contact> > &groupMembersRemoved,
        const Channel::GroupMemberChangeDetails &details)
{
    int ret = -1;

    qDebug() << "group members changed";

    debugContacts();

    QVERIFY(mChan->groupContacts().size() > 1);

    QString roomName = QString("#room%1").arg(mRoomNumber);
    if (groupMembersRemoved.isEmpty()) {
        if (mChan->groupRemotePendingContacts().isEmpty()) {
            QVERIFY(mChan->groupLocalPendingContacts().isEmpty());

            QStringList ids;
            foreach (const QSharedPointer<Contact> &contact, mChan->groupContacts()) {
                ids << contact->id();
            }

            QStringList expectedIds;
            expectedIds << QString("me@") + roomName <<
                QString("alice@") + roomName <<
                QString("bob@") + roomName <<
                QString("chris@") + roomName <<
                QString("anonymous coward@") + roomName;

            if (mChan->groupContacts().count() == 5) {
                ret = 0;
            } else if (mChan->groupContacts().count() == 6) {
                QCOMPARE(details.message(), QString("Invitation accepted"));
                expectedIds << QString("john@") + roomName;
                ret = 3;
            }
            ids.sort();
            expectedIds.sort();
            QCOMPARE(ids, expectedIds);
        } else {
            QCOMPARE(details.actor(), mChan->groupSelfContact());
            QCOMPARE(details.message(), QString("I want to add them"));
            ret = 1;
        }
    } else {
        ret = 2;
    }

    qDebug() << "onGroupMembersChanged exiting with ret" << ret;
    mLoop->exit(ret);
}

void TestChanGroup::debugContacts()
{
    qDebug() << "contacts on group:";
    foreach (const QSharedPointer<Contact> &contact, mChan->groupContacts()) {
        qDebug() << " " << contact->id();
    }

    qDebug() << "local pending contacts on group:";
    foreach (const QSharedPointer<Contact> &contact, mChan->groupLocalPendingContacts()) {
        qDebug() << " " << contact->id();
    }

    qDebug() << "remote pending contacts on group:";
    foreach (const QSharedPointer<Contact> &contact, mChan->groupRemotePendingContacts()) {
        qDebug() << " " << contact->id();
    }
}

void TestChanGroup::initTestCase()
{
    initTestCaseImpl();

    g_type_init();
    g_set_prgname("chan-group");
    tp_debug_set_flags("all");

    gchar *name;
    gchar *connPath;
    GError *error = 0;

    mConnService = EXAMPLE_CSH_CONNECTION(g_object_new(
            EXAMPLE_TYPE_CSH_CONNECTION,
            "account", "me@example.com",
            "protocol", "contacts",
            0));
    QVERIFY(mConnService != 0);
    QVERIFY(tp_base_connection_register(TP_BASE_CONNECTION(mConnService),
                "csh", &name, &connPath, &error));
    QVERIFY(error == 0);

    QVERIFY(name != 0);
    QVERIFY(connPath != 0);

    mConnName = name;
    mConnPath = connPath;

    g_free(name);
    g_free(connPath);

    mConn = new Connection(mConnName, mConnPath);
    QCOMPARE(mConn->isReady(), false);

    mConn->requestConnect();

    QVERIFY(connect(mConn->becomeReady(),
                    SIGNAL(finished(Telepathy::Client::PendingOperation*)),
                    SLOT(expectSuccessfulCall(Telepathy::Client::PendingOperation*))));
    QCOMPARE(mLoop->exec(), 0);
    QCOMPARE(mConn->isReady(), true);

    if (mConn->status() != Connection::StatusConnected) {
        QVERIFY(connect(mConn,
                        SIGNAL(statusChanged(uint, uint)),
                        SLOT(expectConnReady(uint, uint))));
        QCOMPARE(mLoop->exec(), 0);
        QVERIFY(disconnect(mConn,
                           SIGNAL(statusChanged(uint, uint)),
                           this,
                           SLOT(expectConnReady(uint, uint))));
        QCOMPARE(mConn->status(), (uint) Connection::StatusConnected);
    }

    QVERIFY(mConn->requestsInterface() != 0);
}

void TestChanGroup::init()
{
    initImpl();
}

void TestChanGroup::testRequestHandle()
{
    // Test identifiers
    QStringList ids = QStringList() << "#room0" << "#room1";

    // Request handles for the identifiers and wait for the request to process
    PendingHandles *pending = mConn->requestHandles(Telepathy::HandleTypeRoom, ids);
    QVERIFY(connect(pending,
                    SIGNAL(finished(Telepathy::Client::PendingOperation*)),
                    SLOT(expectPendingRoomHandlesFinished(Telepathy::Client::PendingOperation*))));
    QCOMPARE(mLoop->exec(), 0);
    QVERIFY(disconnect(pending,
                       SIGNAL(finished(Telepathy::Client::PendingOperation*)),
                       this,
                       SLOT(expectPendingRoomHandlesFinished(Telepathy::Client::PendingOperation*))));
}

void TestChanGroup::testCreateChannel()
{
    mRoomNumber = 0;
    doTestCreateChannel();
}

void TestChanGroup::testCreateChannelDetailed()
{
    example_csh_connection_set_enable_change_members_detailed(mConnService, true);
    mRoomNumber = 1;
    doTestCreateChannel();
}

void TestChanGroup::doTestCreateChannel()
{
    QVariantMap request;
    request.insert(QLatin1String(TELEPATHY_INTERFACE_CHANNEL ".ChannelType"),
                   TELEPATHY_INTERFACE_CHANNEL_TYPE_TEXT);
    request.insert(QLatin1String(TELEPATHY_INTERFACE_CHANNEL ".TargetHandleType"),
                   Telepathy::HandleTypeRoom);
    request.insert(QLatin1String(TELEPATHY_INTERFACE_CHANNEL ".TargetHandle"),
                   mRoomHandles[mRoomNumber]);

    QVERIFY(connect(mConn->createChannel(request),
                    SIGNAL(finished(Telepathy::Client::PendingOperation*)),
                    SLOT(expectCreateChannelFinished(Telepathy::Client::PendingOperation*))));
    QCOMPARE(mLoop->exec(), 0);

    if (mChan) {
        QVERIFY(connect(mChan->becomeReady(),
                        SIGNAL(finished(Telepathy::Client::PendingOperation*)),
                        SLOT(expectSuccessfulCall(Telepathy::Client::PendingOperation*))));
        QCOMPARE(mLoop->exec(), 0);
        QCOMPARE(mChan->isReady(), true);

        QCOMPARE(mChan->isRequested(), true);
        QCOMPARE(mChan->initiatorContact().isNull(), true);
        QCOMPARE(mChan->groupSelfContact()->id(), QString("me@#room%1").arg(mRoomNumber));

        QCOMPARE(mChan->groupCanAddContacts(), false);
        QCOMPARE(mChan->groupCanRemoveContacts(), false);

        QVERIFY(connect(mChan,
                        SIGNAL(groupFlagsChanged(uint, uint, uint)),
                        SLOT(onChannelGroupFlagsChanged(uint, uint, uint))));
        QCOMPARE(mLoop->exec(), 0);
        QCOMPARE(mChan->groupCanAddContacts(), true);
        QCOMPARE(mChan->groupCanRemoveContacts(), true);

        debugContacts();

        QVERIFY(connect(mChan,
                        SIGNAL(groupMembersChanged(
                                const QList<QSharedPointer<Contact> > &,
                                const QList<QSharedPointer<Contact> > &,
                                const QList<QSharedPointer<Contact> > &,
                                const QList<QSharedPointer<Contact> > &,
                                const Channel::GroupMemberChangeDetails &)),
                        SLOT(onGroupMembersChanged(
                                const QList<QSharedPointer<Contact> > &,
                                const QList<QSharedPointer<Contact> > &,
                                const QList<QSharedPointer<Contact> > &,
                                const QList<QSharedPointer<Contact> > &,
                                const Channel::GroupMemberChangeDetails &))));
        QCOMPARE(mLoop->exec(), 0);

        QStringList ids = QStringList() <<
            QString("john@#room%1").arg(mRoomNumber) <<
            QString("mary@#room%1").arg(mRoomNumber) <<
            QString("another anonymous coward@#room%1").arg(mRoomNumber);
        QVERIFY(connect(mConn->requestHandles(Telepathy::HandleTypeContact, ids),
                        SIGNAL(finished(Telepathy::Client::PendingOperation*)),
                        SLOT(expectPendingContactHandlesFinished(Telepathy::Client::PendingOperation*))));
        QCOMPARE(mLoop->exec(), 0);

        // Wait for the contacts to be built
        QVERIFY(connect(mConn->contactManager()->contactsForHandles(mContactHandles),
                        SIGNAL(finished(Telepathy::Client::PendingOperation*)),
                        SLOT(expectPendingContactsFinished(Telepathy::Client::PendingOperation*))));
        QCOMPARE(mLoop->exec(), 0);

        QCOMPARE(mContacts.size(), 3);
        QCOMPARE(mContacts.first()->id(), QString("john@#room%1").arg(mRoomNumber));

        mChan->groupAddContacts(mContacts, "I want to add them");

        // members changed should be emitted twice now, one for adding john to
        // remote pending and one for adding it to current members

        // expect contacts to be added to remote pending
        QCOMPARE(mLoop->exec(), 1);
        QCOMPARE(mLoop->exec(), 1);
        QCOMPARE(mLoop->exec(), 1);

        QString roomName = QString("#room%1").arg(mRoomNumber);
        QStringList expectedIds;
        expectedIds << QString("john@") + roomName <<
                QString("mary@") + roomName <<
                QString("another anonymous coward@") + roomName;

        ids.clear();
        foreach (const QSharedPointer<Contact> &contact, mChan->groupRemotePendingContacts()) {
            ids << contact->id();
        }

        ids.sort();
        expectedIds.sort();
        QCOMPARE(ids, expectedIds);

        QList<QSharedPointer<Contact> > toRemove;
        toRemove.append(mContacts[1]);
        toRemove.append(mContacts[2]);
        mChan->groupRemoveContacts(toRemove, "I want to remove some of them");

        // expect mary and another anonymous coward to reject invite
        QCOMPARE(mLoop->exec(), 2);

        example_csh_connection_accept_invitations(mConnService);

        // expect john to accept invite
        QCOMPARE(mLoop->exec(), 3);

        expectedIds.clear();
        expectedIds << QString("me@") + roomName <<
                QString("alice@") + roomName <<
                QString("bob@") + roomName <<
                QString("chris@") + roomName <<
                QString("anonymous coward@") + roomName <<
                QString("john@") + roomName;

        toRemove.clear();
        ids.clear();
        foreach (const QSharedPointer<Contact> &contact, mChan->groupContacts()) {
            ids << contact->id();
            if (contact != mChan->groupSelfContact() && toRemove.isEmpty()) {
                toRemove.append(contact);
            }
        }

        ids.sort();
        expectedIds.sort();
        QCOMPARE(ids, expectedIds);

        mChan->groupRemoveContacts(toRemove, "Checking removal of a contact in current list");
        QCOMPARE(mLoop->exec(), 2);

        ids.clear();
        foreach (const QSharedPointer<Contact> &contact, mChan->groupContacts()) {
            ids << contact->id();
        }

        ids.sort();
        expectedIds.removeOne(toRemove.first()->id());
        QCOMPARE(ids, expectedIds);

        delete mChan;
        mChan = 0;
    }
}

void TestChanGroup::cleanup()
{
    cleanupImpl();
}

void TestChanGroup::cleanupTestCase()
{
    if (mConn != 0) {
        // Disconnect and wait for the readiness change
        QVERIFY(connect(mConn->requestDisconnect(),
                        SIGNAL(finished(Telepathy::Client::PendingOperation*)),
                        SLOT(expectSuccessfulCall(Telepathy::Client::PendingOperation*))));
        QCOMPARE(mLoop->exec(), 0);

        if (mConn->isValid()) {
            QVERIFY(connect(mConn,
                            SIGNAL(invalidated(Telepathy::Client::DBusProxy *,
                                               const QString &, const QString &)),
                            SLOT(expectConnInvalidated())));
            QCOMPARE(mLoop->exec(), 0);
        }

        delete mConn;
        mConn = 0;
    }

    if (mConnService != 0) {
        g_object_unref(mConnService);
        mConnService = 0;
    }

    cleanupTestCaseImpl();
}

QTEST_MAIN(TestChanGroup)
#include "_gen/chan-group.cpp.moc.hpp"
