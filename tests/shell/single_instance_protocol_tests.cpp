// Pure tests for shell/single_instance_protocol.h (no Qt, no OS). CI-safe.
#include "shell/single_instance_protocol.h"

#include <cstdio>
#include <string>

namespace p = subrec::shell::single_instance_protocol;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main()
{
    CHECK(p::requestLine(true) == "show\n");
    CHECK(p::requestLine(false) == "noop\n");
    CHECK(p::ackLine() == "ok\n");

    CHECK(p::parseRequest("show\n") == p::Parsed::Show);
    CHECK(p::parseRequest("noop\r\n") == p::Parsed::Noop);
    CHECK(p::parseRequest("show") == p::Parsed::Show);
    CHECK(p::parseRequest("SHOW\n") == p::Parsed::Invalid);
    CHECK(p::parseRequest("\n") == p::Parsed::Invalid);
    CHECK(p::parseRequest("") == p::Parsed::Invalid);
    CHECK(p::parseRequest("showx\n") == p::Parsed::Invalid);
    CHECK(p::parseRequest(std::string(64, 'a')) == p::Parsed::Invalid);
    // Round trip
    CHECK(p::parseRequest(p::requestLine(true)) == p::Parsed::Show);
    CHECK(p::parseRequest(p::requestLine(false)) == p::Parsed::Noop);

    CHECK(p::isAck("ok\n"));
    CHECK(p::isAck("ok"));
    CHECK(!p::isAck("no\n"));
    CHECK(!p::isAck(""));

    CHECK(p::serverNameFor("alice") == "SubRec.SingleInstance.alice");
    CHECK(p::serverNameFor("") == "SubRec.SingleInstance.unknown");
    CHECK(p::serverNameFor("a b\\c") == "SubRec.SingleInstance.a_b_c");
    CHECK(p::serverNameFor("Zhang.Y-1_") == "SubRec.SingleInstance.Zhang.Y-1_");
    // Non-ASCII (UTF-8) bytes are replaced, never passed into the pipe name.
    CHECK(p::serverNameFor("\xE5\xBC\xA0") == "SubRec.SingleInstance.___");

    if (failures == 0) std::printf("single_instance_protocol_tests: all passed\n");
    return failures == 0 ? 0 : 1;
}
