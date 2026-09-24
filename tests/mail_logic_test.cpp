// mail_logic_test.cpp - headless functional tests for the nefuOS mail app
// (core/apps/email.h). Exercises persistence round-trip, wrap, message ops,
// compose ops, and visible-list mapping against a real in-memory VFS.
// Build: see tests/build_mail_test.ps1
#include <cstdio>
#include <cstring>
#include "../core/vfs/vfs.h"
#include "../core/apps/email.h"

using namespace nefu;
using namespace nefu::apps;

static int s_fails = 0;
static int s_checks = 0;

#define CHECK(cond, msg) \
    do { s_checks++; if (!(cond)) { s_fails++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main() {
    VFS vfs;
    g_vfs = &vfs;
    // The real OS calls ensure_standard_dirs() at boot; recreate that here.
    vfs.mkdir("/home");
    vfs.mkdir("/home/user");

    // EmailState is ~1.15MB, so allocate on the heap like the real app does.
    EmailState* st = new EmailState();
    EmailState* st2 = new EmailState();
    EmailState* st3 = new EmailState();
    EmailState* st4 = new EmailState();
    EmailState* st5 = new EmailState();

    printf("[1] constructor\n");
    CHECK(strcmp(st->folders[EF_INBOX].name, "Inbox") == 0, "inbox name");
    CHECK(st->folders[EF_SENT].msg_count == 0 && st->folders[EF_DRAFTS].msg_count == 0, "empty folders");
    CHECK(st->acct.smtp_port == 587 && st->acct.pop_port == 110, "default ports");
    CHECK(st->view == EV_LIST && st->cur_folder == EF_INBOX, "initial view");

    printf("[2] seed\n");
    mail_seed(st);
    CHECK(st->folders[EF_INBOX].msg_count == 3, "inbox 3");
    CHECK(st->folders[EF_INBOX].unread_count == 3, "inbox unread 3");
    CHECK(st->folders[EF_SENT].msg_count == 1, "sent 1");
    CHECK(st->folders[EF_DRAFTS].msg_count == 1, "drafts 1");
    CHECK(st->folders[EF_DRAFTS].messages[0].subject[0] != 0, "draft subject");

    printf("[3] save to VFS\n");
    mail_save(st);
    FSNode* f = g_vfs->resolve("/home/user/Mail/inbox.mail");
    CHECK(f && !f->is_dir && f->size > 0, "inbox.mail written");
    f = g_vfs->resolve("/home/user/Mail/sent.mail");
    CHECK(f && f->size > 0, "sent.mail written");
    f = g_vfs->resolve("/home/user/Mail/account.conf");
    CHECK(f && f->size > 0, "account.conf written");
    CHECK(f && strstr((const char*)f->data, "smtp_port=587") != 0, "account port round-trip");

    printf("[4] load round-trip\n");
    mail_load(st2);
    CHECK(st2->folders[EF_INBOX].msg_count == 3, "reloaded inbox 3");
    CHECK(st2->folders[EF_INBOX].unread_count == 3, "reloaded unread 3");
    CHECK(st2->folders[EF_SENT].msg_count == 1, "reloaded sent 1");
    CHECK(st2->folders[EF_DRAFTS].msg_count == 1, "reloaded drafts 1");
    CHECK(strcmp(st2->folders[EF_INBOX].messages[0].subject, "Welcome to nefuOS!") == 0, "subject match");
    CHECK(strcmp(st2->folders[EF_INBOX].messages[1].body, st->folders[EF_INBOX].messages[1].body) == 0, "body match");
    CHECK(st2->folders[EF_INBOX].messages[1].starred == true, "star flag preserved");
    CHECK(st2->folders[EF_INBOX].messages[0].read == false, "read flag preserved");

    printf("[5] message operations\n");
    st2->cur_folder = EF_INBOX;
    st2->cur_msg = 0;
    mail_open_msg(st2);
    CHECK(st2->view == EV_READ, "open goes to read view");
    CHECK(st2->folders[EF_INBOX].unread_count == 2, "unread decremented");
    int star_before = 0;
    for (int i = 0; i < st2->folders[EF_INBOX].msg_count; i++) if (st2->folders[EF_INBOX].messages[i].starred) star_before++;
    mail_toggle_star(st2);
    mail_rebuild_star(st2);
    CHECK(st2->star_cnt == star_before + 1, "star count +1");
    st2->view = EV_LIST;
    mail_delete_current(st2);
    CHECK(st2->folders[EF_INBOX].msg_count == 2, "inbox after delete 2");
    CHECK(st2->folders[EF_TRASH].msg_count == 1, "trash 1");
    CHECK(st2->folders[EF_INBOX].unread_count == 2, "unread after delete (2 remain unread)");
    st2->cur_folder = EF_TRASH;
    st2->cur_msg = 0;
    int tf, ti;
    vis_src(st2, 0, tf, ti);
    mail_move_msg(st2, EF_TRASH, ti, EF_INBOX);
    CHECK(st2->folders[EF_TRASH].msg_count == 0, "trash empty after restore");
    CHECK(st2->folders[EF_INBOX].msg_count == 3, "inbox restored 3");

    printf("[6] search / visible mapping\n");
    st2->cur_folder = EF_INBOX;
    strcpy(st2->search, "update");
    CHECK(vis_total(st2) == 1, "search finds 1");
    EmailMessage* m = vis_msg(st2, 0);
    CHECK(m && strstr(m->subject, "update") != 0, "search result subject");
    st2->search[0] = 0;
    CHECK(vis_total(st2) == 3, "empty search shows all");

    printf("[7] compose ops\n");
    compose_start(st2, true);
    int cur = 0;
    mail_buf_insert(st2->comp_to, (int)sizeof(st2->comp_to), cur, "alice@example.com");
    mail_buf_insert(st2->comp_subject, (int)sizeof(st2->comp_subject), st2->comp_cursor[2], "Hello Alice");
    mail_buf_insert(st2->comp_body, (int)sizeof(st2->comp_body), st2->comp_cursor[3], "Line one\nLine two");
    CHECK(strcmp(st2->comp_to, "alice@example.com") == 0, "compose to");
    compose_save_draft(st2);
    CHECK(st2->folders[EF_DRAFTS].msg_count == 2, "drafts 2 after save");
    CHECK(st2->view == EV_LIST && st2->cur_folder == EF_DRAFTS, "back to drafts list");
    int ff, ii;
    vis_src(st2, st2->cur_msg, ff, ii);
    compose_from_draft(st2, ii);
    CHECK(strcmp(st2->comp_to, "alice@example.com") == 0, "draft to restored");
    CHECK(strstr(st2->comp_body, "Line two") != 0, "draft body restored");

    printf("[8] send (bare path: local Sent)\n");
    compose_start(st2, true);
    cur = 0;
    mail_buf_insert(st2->comp_to, (int)sizeof(st2->comp_to), cur, "bob@example.com");
    int cur2 = 0;
    mail_buf_insert(st2->comp_subject, (int)sizeof(st2->comp_subject), cur2, "Test message");
    int cur3 = 0;
    mail_buf_insert(st2->comp_body, (int)sizeof(st2->comp_body), cur3, "Hi Bob");
    int sent_before = st2->folders[EF_SENT].msg_count;
    mail_send_current(st2);
    CHECK(st2->folders[EF_SENT].msg_count == sent_before + 1, "sent +1");
    CHECK(st2->view == EV_LIST && st2->cur_folder == EF_SENT, "after send view sent");
    CHECK(strcmp(st2->folders[EF_SENT].messages[st2->folders[EF_SENT].msg_count - 1].to, "bob@example.com") == 0, "sent recipient");

    printf("[9] reply / forward prefixes\n");
    st2->cur_folder = EF_INBOX;
    st2->cur_msg = 0;
    st2->search[0] = 0;
    // newest message sorts to the top of the visible order (date desc)
    const char* expected_from = vis_msg(st2, 0)->from;
    st2->view = EV_READ;
    compose_reply(st2);
    CHECK(strncmp(st2->comp_subject, "Re: ", 4) == 0, "reply Re: prefix");
    CHECK(strcmp(st2->comp_to, expected_from) == 0, "reply to sender");
    compose_forward(st2);
    CHECK(strncmp(st2->comp_subject, "Fwd: ", 5) == 0, "forward Fwd: prefix");

    printf("[10] wrap\n");
    int starts[32];
    const char* text = "hello world foo";
    int lines = mail_wrap(text, 40, starts, 32);
    CHECK(lines >= 2, "wrap splits long line");
    CHECK(starts[0] == 0, "wrap start 0");
    bool fits = true;
    for (int i = 0; i < lines - 1; i++) {
        int end = starts[i + 1] - 1;
        if (mail_prefix_px(text, starts[i], end) > 40) { fits = false; break; }
    }
    CHECK(fits, "wrapped segments fit width");
    CHECK(mail_wrap("", 40, starts, 32) == 1, "empty wraps to 1 line");
    lines = mail_wrap("a\nb", 400, starts, 32);
    CHECK(lines == 2, "newline splits lines");

    printf("[11] persistence round-trip after ops\n");
    mail_save(st2);
    mail_load(st3);
    CHECK(st3->folders[EF_INBOX].msg_count == 3, "post-op inbox 3");
    CHECK(st3->folders[EF_SENT].msg_count == 2, "post-op sent 2");
    CHECK(st3->folders[EF_DRAFTS].msg_count == 2, "post-op drafts 2");
    CHECK(st3->folders[EF_TRASH].msg_count == 0, "post-op trash 0");
    CHECK(st3->folders[EF_SENT].messages[1].body[0] != 0, "sent body persisted");

    printf("[12] account commit + persistence\n");
    strncpy(st4->acc_buf[1], "me@corp.example", sizeof(st4->acc_buf[1]) - 1);
    strncpy(st4->acc_buf[2], "smtp.corp.example", sizeof(st4->acc_buf[2]) - 1);
    strncpy(st4->acc_buf[3], "587", sizeof(st4->acc_buf[3]) - 1);
    strncpy(st4->acc_buf[4], "pop.corp.example", sizeof(st4->acc_buf[4]) - 1);
    strncpy(st4->acc_buf[5], "110", sizeof(st4->acc_buf[5]) - 1);
    strncpy(st4->acc_buf[6], "me", sizeof(st4->acc_buf[6]) - 1);
    strncpy(st4->acc_buf[7], "s3cret", sizeof(st4->acc_buf[7]) - 1);
    strncpy(st4->acct.display, st4->acc_buf[0], sizeof(st4->acct.display) - 1);
    strncpy(st4->acct.email, st4->acc_buf[1], sizeof(st4->acct.email) - 1);
    strncpy(st4->acct.smtp_host, st4->acc_buf[2], sizeof(st4->acct.smtp_host) - 1);
    strncpy(st4->acct.pop_host, st4->acc_buf[4], sizeof(st4->acct.pop_host) - 1);
    strncpy(st4->acct.username, st4->acc_buf[6], sizeof(st4->acct.username) - 1);
    strncpy(st4->acct.password, st4->acc_buf[7], sizeof(st4->acct.password) - 1);
    st4->acct.smtp_port = atoi(st4->acc_buf[3]);
    st4->acct.pop_port = atoi(st4->acc_buf[5]);
    st4->acct.configured = st4->acct.email[0] != 0;
    mail_save(st4);
    mail_load(st5);
    CHECK(st5->acct.configured, "account configured after reload");
    CHECK(strcmp(st5->acct.email, "me@corp.example") == 0, "account email persisted");
    CHECK(strcmp(st5->acct.smtp_host, "smtp.corp.example") == 0, "smtp host persisted");
    CHECK(strcmp(st5->acct.password, "s3cret") == 0, "password persisted");
    CHECK(st5->acct.smtp_port == 587 && st5->acct.pop_port == 110, "ports persisted");

    printf("[13] attachment + signature persistence round-trip\n");
    EmailState* st6 = new EmailState();
    EmailFolder& d6 = st6->folders[EF_DRAFTS];
    EmailMessage m6;
    memset(&m6, 0, sizeof(m6));
    strcpy(m6.to, "x@y.z");
    strcpy(m6.subject, "with att");
    strcpy(m6.body, "body");
    m6.att_cnt = 1;
    strcpy(m6.att[0].name, "doc.pdf");
    strcpy(m6.att[0].path, "/home/user/doc.pdf");
    strcpy(m6.att[0].type, "application/pdf");
    m6.att[0].size = 1234;
    mail_now(m6.date, sizeof(m6.date));
    m6.read = true;
    d6.messages[d6.msg_count++] = m6;
    strncpy(st6->acct.signature, "-- sent from nefuOS", sizeof(st6->acct.signature) - 1);
    mail_save(st6);
    EmailState* st7 = new EmailState();
    mail_load(st7);
    CHECK(st7->folders[EF_DRAFTS].msg_count == 1, "att draft reloaded");
    if (st7->folders[EF_DRAFTS].msg_count > 0) {
        EmailMessage& r = st7->folders[EF_DRAFTS].messages[0];
        CHECK(r.att_cnt == 1, "att count round-trip");
        CHECK(r.att_cnt > 0 && strcmp(r.att[0].name, "doc.pdf") == 0, "att name round-trip");
        CHECK(r.att_cnt > 0 && strcmp(r.att[0].type, "application/pdf") == 0, "att type round-trip");
        CHECK(r.att_cnt > 0 && r.att[0].size == 1234, "att size round-trip");
    }
    CHECK(strcmp(st7->acct.signature, "-- sent from nefuOS") == 0, "signature round-trip");
    delete st6;
    delete st7;

    delete st;
    delete st2;
    delete st3;
    delete st4;
    delete st5;

    printf("\n%d checks, %d failures\n", s_checks, s_fails);
    return s_fails == 0 ? 0 : 1;
}
