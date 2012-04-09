#include "rpc/mailbox/mailbox.hpp"

#include <sstream>

#include "logger.hpp"

/* raw_mailbox_t */

raw_mailbox_t::address_t::address_t() :
    peer(peer_id_t()), thread(-1), mailbox_id(-1) { }

raw_mailbox_t::address_t::address_t(const address_t &a) :
    peer(a.peer), thread(a.thread), mailbox_id(a.mailbox_id) { }

bool raw_mailbox_t::address_t::is_nil() const {
    return peer.is_nil();
}

peer_id_t raw_mailbox_t::address_t::get_peer() const {
    rassert(!is_nil(), "A nil address has no peer");
    return peer;
}

raw_mailbox_t::raw_mailbox_t(mailbox_manager_t *m, const boost::function<void(read_stream_t *, const boost::function<void()> &)> &fun) :
    manager(m),
    mailbox_id(manager->mailbox_tables.get()->next_mailbox_id++),
    callback(fun)
{
    rassert(manager->mailbox_tables.get()->mailboxes.find(mailbox_id) ==
        manager->mailbox_tables.get()->mailboxes.end());
    manager->mailbox_tables.get()->mailboxes[mailbox_id] = this;
}

raw_mailbox_t::~raw_mailbox_t() {
    assert_thread();
    rassert(manager->mailbox_tables.get()->mailboxes[mailbox_id] == this);
    manager->mailbox_tables.get()->mailboxes.erase(mailbox_id);
}

raw_mailbox_t::address_t raw_mailbox_t::get_address() {
    address_t a;
    a.peer = manager->get_connectivity_service()->get_me();
    a.thread = home_thread();
    a.mailbox_id = mailbox_id;
    return a;
}

void send(mailbox_manager_t *src, raw_mailbox_t::address_t dest, boost::function<void(write_stream_t *)> writer) {
    rassert(src);
    rassert(!dest.is_nil());

    src->message_service->send_message(dest.peer,
        boost::bind(
            &mailbox_manager_t::write_mailbox_message,
            _1,
            dest.thread,
            dest.mailbox_id,
            writer
            ));
}

mailbox_manager_t::mailbox_manager_t(message_service_t *ms) :
    message_service(ms)
    { }

mailbox_manager_t::mailbox_table_t::mailbox_table_t() {
    next_mailbox_id = 0;
}

mailbox_manager_t::mailbox_table_t::~mailbox_table_t() {
    rassert(mailboxes.empty(), "Please destroy all mailboxes before destroying "
        "the cluster");
}

raw_mailbox_t *mailbox_manager_t::mailbox_table_t::find_mailbox(raw_mailbox_t::id_t id) {
    std::map<raw_mailbox_t::id_t, raw_mailbox_t *>::iterator it = mailboxes.find(id);
    if (it == mailboxes.end()) {
        rassert(id < next_mailbox_id, "Not only does the requested mailbox not "
            "currently exist, but it never existed; the given mailbox ID has "
            "yet to be assigned to a mailbox.");
        return NULL;
    } else {
        return (*it).second;
    }
}

void mailbox_manager_t::write_mailbox_message(write_stream_t *stream, int dest_thread, raw_mailbox_t::id_t dest_mailbox_id, boost::function<void(write_stream_t *)> writer) {
    write_message_t msg;
    int32_t dest_thread_32_bit = dest_thread;
    msg << dest_thread_32_bit;
    msg << dest_mailbox_id;

    // TODO: Maybe pass write_message_t to writer... eventually.
    int res = send_write_message(stream, &msg);

    if (res) { throw fake_archive_exc_t(); }

    writer(stream);
}

void mailbox_manager_t::on_message(UNUSED peer_id_t source_peer, read_stream_t *stream) {
    int dest_thread;
    raw_mailbox_t::id_t dest_mailbox_id;
    {
        int res = deserialize(stream, &dest_thread);
        if (!res) { res = deserialize(stream, &dest_mailbox_id); }

        if (res) {
            throw fake_archive_exc_t();
        }
    }

    /* TODO: This is probably horribly inefficient; we switch to another
    thread and back before we parse the next message. */
    on_thread_t thread_switcher(dest_thread);

    raw_mailbox_t *mbox = mailbox_tables.get()->find_mailbox(dest_mailbox_id);
    if (mbox) {
        cond_t done_cond;
        boost::function<void()> done_fun = boost::bind(&cond_t::pulse, &done_cond);
        coro_t::spawn_sometime(boost::bind(mbox->callback, stream, done_fun));
        done_cond.wait_lazily_unordered();
    } else {
        /* Print a warning message */
        raw_mailbox_t::address_t dest_address;
        dest_address.peer = message_service->get_connectivity_service()->get_me();
        dest_address.thread = dest_thread;
        dest_address.mailbox_id = dest_mailbox_id;
        std::ostringstream buffer;
        buffer << dest_address;
        logDBG("Message dropped because mailbox %s no longer exists. (This doesn't necessarily indicate a bug.)\n", buffer.str().c_str());
    }
}
