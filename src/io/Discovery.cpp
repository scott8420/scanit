#include "io/Discovery.hpp"
#include "Log.hpp"

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

// io/Discovery.cpp -- Avahi's threaded poll: browsers and resolvers run on
// Avahi's own thread; we collect under a mutex, sleep the timeout, then stop
// the poll before freeing anything.

namespace scanit::io {

namespace {

struct Ctx {
    std::mutex         mu;
    std::vector<Found> found;
};

void on_resolve(AvahiServiceResolver* r, AvahiIfIndex, AvahiProtocol proto,
                AvahiResolverEvent event, const char* name, const char* type,
                const char*, const char* host, const AvahiAddress* addr, uint16_t port,
                AvahiStringList* txt, AvahiLookupResultFlags, void* user) {
    auto* ctx = static_cast<Ctx*>(user);
    if (event == AVAHI_RESOLVER_FOUND && addr) {
        char a[AVAHI_ADDRESS_STR_MAX];
        avahi_address_snprint(a, sizeof a, addr);
        Found f;
        f.name = name ? name : "";
        f.host = host ? host : "";
        f.address = a;
        f.port = port;
        f.tls = type && std::string(type).find("_uscans") != std::string::npos;
        if (AvahiStringList* rs = avahi_string_list_find(txt, "rs")) {
            char* key = nullptr; char* val = nullptr;
            if (avahi_string_list_get_pair(rs, &key, &val, nullptr) == 0) {
                if (val) f.rs = val;
                avahi_free(key);
                avahi_free(val);
            }
        }
        if (f.rs.empty()) f.rs = "eSCL";
        std::lock_guard lk(ctx->mu);
        // One entry per (name, tls); IPv4 wins over IPv6.
        auto same = std::find_if(ctx->found.begin(), ctx->found.end(), [&](const Found& g) {
            return g.name == f.name && g.tls == f.tls;
        });
        if (same == ctx->found.end()) ctx->found.push_back(f);
        else if (proto == AVAHI_PROTO_INET) *same = f;
        if (auto lg = log::get(log::Area::Io))
            lg->info("found {} at {}:{} ({}, rs={})", f.name, f.address, f.port,
                     f.tls ? "https" : "http", f.rs);
    }
    avahi_service_resolver_free(r);
}

void on_browse(AvahiServiceBrowser* b, AvahiIfIndex iface, AvahiProtocol proto,
               AvahiBrowserEvent event, const char* name, const char* type,
               const char* domain, AvahiLookupResultFlags, void* user) {
    if (event != AVAHI_BROWSER_NEW) return;
    AvahiClient* c = avahi_service_browser_get_client(b);
    avahi_service_resolver_new(c, iface, proto, name, type, domain, AVAHI_PROTO_UNSPEC,
                               AvahiLookupFlags(0), on_resolve, user);
}

void on_client(AvahiClient*, AvahiClientState, void*) {}

}  // namespace

std::vector<Found> discover(int timeout_ms, std::string* err) {
    Ctx ctx;
    AvahiThreadedPoll* poll = avahi_threaded_poll_new();
    if (!poll) { if (err) *err = "could not start Avahi's poll"; return {}; }
    int e = 0;
    AvahiClient* client = avahi_client_new(avahi_threaded_poll_get(poll), AvahiClientFlags(0),
                                           on_client, nullptr, &e);
    if (!client) {
        if (err) *err = std::string("Avahi: ") + avahi_strerror(e) +
                        " (is avahi-daemon running?)";
        avahi_threaded_poll_free(poll);
        return {};
    }
    AvahiServiceBrowser* b1 = avahi_service_browser_new(
        client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_uscan._tcp", nullptr,
        AvahiLookupFlags(0), on_browse, &ctx);
    AvahiServiceBrowser* b2 = avahi_service_browser_new(
        client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_uscans._tcp", nullptr,
        AvahiLookupFlags(0), on_browse, &ctx);
    avahi_threaded_poll_start(poll);
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    avahi_threaded_poll_stop(poll);
    if (b1) avahi_service_browser_free(b1);
    if (b2) avahi_service_browser_free(b2);
    avahi_client_free(client);
    avahi_threaded_poll_free(poll);

    std::lock_guard lk(ctx.mu);
    // Plain HTTP first: no certificate to argue with.
    std::stable_sort(ctx.found.begin(), ctx.found.end(),
                     [](const Found& a, const Found& b) { return !a.tls && b.tls; });
    return ctx.found;
}

}  // namespace scanit::io
