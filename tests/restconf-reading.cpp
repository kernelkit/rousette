/*
 * Copyright (C) 2023 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 */

static const auto SERVER_PORT = "10081";
#include "tests/aux-utils.h"
#include <nghttp2/asio_http2.h>
#include <spdlog/spdlog.h>
#include "restconf/Server.h"
#include "tests/datastoreUtils.h"

TEST_CASE("reading data")
{
    spdlog::set_level(spdlog::level::trace);
    auto srConn = sysrepo::Connection{};
    auto srSess = srConn.sessionStart(sysrepo::Datastore::Running);
    srSess.sendRPC(srSess.getContext().newPath("/ietf-factory-default:factory-reset"));
    auto nacmGuard = manageNacm(srSess);

    SUBSCRIBE_MODULE(sub1, srSess, "example");
    SUBSCRIBE_MODULE(sub2, srSess, "ietf-system");

    auto server = rousette::restconf::Server{srConn, SERVER_ADDRESS, SERVER_PORT};

    // something we can read
    srSess.switchDatastore(sysrepo::Datastore::Operational);
    srSess.setItem("/ietf-system:system/contact", "contact");
    srSess.setItem("/ietf-system:system/hostname", "hostname");
    srSess.setItem("/ietf-system:system/location", "location");
    srSess.setItem("/ietf-system:system/clock/timezone-utc-offset", "2");
    srSess.setItem("/ietf-system:system/radius/server[name='a']/udp/address", "1.1.1.1");
    srSess.setItem("/ietf-system:system/radius/server[name='a']/udp/shared-secret", "shared-secret");
    srSess.applyChanges();

    srSess.switchDatastore(sysrepo::Datastore::Running);
    srSess.setItem("/example:top-level-leaf", "moo");
    srSess.applyChanges();

    // setup real-like NACM
    setupRealNacm(srSess);

    DOCTEST_SUBCASE("unsupported methods")
    {
        // we do not support these http methods yet
        for (const auto& httpMethod : {"OPTIONS"s, "PATCH"s}) {
            CAPTURE(httpMethod);
            REQUIRE(clientRequest(httpMethod, RESTCONF_DATA_ROOT "/ietf-system:system", "", {AUTH_ROOT}) == Response{405, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "operation-not-supported",
        "error-message": "Method not allowed."
      }
    ]
  }
}
)"});
        }
    }

    DOCTEST_SUBCASE("entire datastore")
    {
        // this relies on a NACM rule for anonymous access that filters out "a lot of stuff"
        REQUIRE(get(RESTCONF_DATA_ROOT, {}) == Response{200, jsonHeaders, R"({
  "example:top-level-leaf": "moo",
  "ietf-restconf-monitoring:restconf-state": {
    "capabilities": {
      "capability": [
        "urn:ietf:params:restconf:capability:defaults:1.0?basic-mode=explicit",
        "urn:ietf:params:restconf:capability:depth:1.0",
        "urn:ietf:params:restconf:capability:with-defaults:1.0"
      ]
    }
  },
  "ietf-system:system": {
    "contact": "contact",
    "hostname": "hostname",
    "location": "location"
  }
}
)"});

        REQUIRE(get(RESTCONF_ROOT_DS("operational"), {}) == Response{200, jsonHeaders, R"({
  "example:top-level-leaf": "moo",
  "ietf-restconf-monitoring:restconf-state": {
    "capabilities": {
      "capability": [
        "urn:ietf:params:restconf:capability:defaults:1.0?basic-mode=explicit",
        "urn:ietf:params:restconf:capability:depth:1.0",
        "urn:ietf:params:restconf:capability:with-defaults:1.0"
      ]
    }
  },
  "ietf-system:system": {
    "contact": "contact",
    "hostname": "hostname",
    "location": "location"
  }
}
)"});

        REQUIRE(get(RESTCONF_ROOT_DS("running"), {}) == Response{200, jsonHeaders, R"({
  "example:top-level-leaf": "moo"
}
)"});
    }

    DOCTEST_SUBCASE("subtree")
    {
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system/clock", {AUTH_DWDM}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "clock": {
      "timezone-utc-offset": 2
    }
  }
}
)"});
    }

    DOCTEST_SUBCASE("Basic querying of lists")
    {
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system/radius/server=a", {AUTH_DWDM}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "radius": {
      "server": [
        {
          "name": "a",
          "udp": {
            "address": "1.1.1.1",
            "shared-secret": "shared-secret"
          }
        }
      ]
    }
  }
}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system/radius/server=a/udp/address", {AUTH_DWDM}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "radius": {
      "server": [
        {
          "name": "a",
          "udp": {
            "address": "1.1.1.1"
          }
        }
      ]
    }
  }
}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system/radius?depth=1", {AUTH_DWDM}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "radius": {
      "server": [
        {
          "name": "a"
        }
      ]
    }
  }
}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system/radius?depth=1&depth=1", {AUTH_DWDM}) == Response{400, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "protocol",
        "error-tag": "invalid-value",
        "error-message": "Query parameter 'depth' already specified"
      }
    ]
  }
}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system/radius?depth=unbounded", {AUTH_DWDM}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "radius": {
      "server": [
        {
          "name": "a",
          "udp": {
            "address": "1.1.1.1",
            "shared-secret": "shared-secret"
          }
        }
      ]
    }
  }
}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system/radius/server=b", {AUTH_DWDM}) == Response{404, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "invalid-value",
        "error-message": "No data from sysrepo."
      }
    ]
  }
}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system/radius/server=a,b", {AUTH_DWDM}) == Response{400, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "operation-failed",
        "error-message": "List '/ietf-system:system/radius/server' requires 1 keys"
      }
    ]
  }
}
)"});
    }

    DOCTEST_SUBCASE("RPCs")
    {
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system-restart", {AUTH_DWDM}) == Response{405, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "protocol",
        "error-tag": "operation-not-supported",
        "error-message": "'/ietf-system:system-restart' is an RPC/Action node"
      }
    ]
  }
}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/example:tlc/list=eth0/example-action", {AUTH_DWDM}) == Response{405, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "protocol",
        "error-tag": "operation-not-supported",
        "error-message": "'/example:tlc/list/example-action' is an RPC/Action node"
      }
    ]
  }
}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/example:tlc/list=eth0/example-action/i", {AUTH_DWDM}) == Response{400, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "operation-failed",
        "error-message": "'/example:tlc/list/example-action' is an RPC/Action node, any child of it can't be requested"
      }
    ]
  }
}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/example:tlc/list=eth0/example-action/o", {AUTH_DWDM}) == Response{400, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "operation-failed",
        "error-message": "'/example:tlc/list/example-action' is an RPC/Action node, any child of it can't be requested"
      }
    ]
  }
}
)"});
    }

    DOCTEST_SUBCASE("Test data formats preference")
    {
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "contact": "contact",
    "hostname": "hostname",
    "location": "location"
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"accept", "text/plain"}}) == Response{406, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "operation-not-supported",
        "error-message": "No requested format supported"
      }
    ]
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"accept", "application/yang-data"}}) == Response{406, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "operation-not-supported",
        "error-message": "No requested format supported"
      }
    ]
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"content-type", "text/plain"}}) == Response{415, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "operation-not-supported",
        "error-message": "content-type format value not supported"
      }
    ]
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"accept", "application/yang-data+json"}}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "contact": "contact",
    "hostname": "hostname",
    "location": "location"
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"content-type", "application/yang-data+json"}}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "contact": "contact",
    "hostname": "hostname",
    "location": "location"
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"content-type", "application/yang-data+jsonx"}}) == Response{415, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "operation-not-supported",
        "error-message": "content-type format value not supported"
      }
    ]
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"content-type", "application/yang-data+xmlx"}}) == Response{415, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "operation-not-supported",
        "error-message": "content-type format value not supported"
      }
    ]
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"content-type", "application/yang-data+json;charset=utf8"}}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "contact": "contact",
    "hostname": "hostname",
    "location": "location"
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"accept", "application/yang-data+xml"}}) == Response{200, xmlHeaders, R"(<system xmlns="urn:ietf:params:xml:ns:yang:ietf-system">
  <contact>contact</contact>
  <hostname>hostname</hostname>
  <location>location</location>
</system>
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"accept", "application/yang-data+xml,application/yang-data+json"}}) == Response{200, xmlHeaders, R"(<system xmlns="urn:ietf:params:xml:ns:yang:ietf-system">
  <contact>contact</contact>
  <hostname>hostname</hostname>
  <location>location</location>
</system>
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"content-type", "application/yang-data+xml"}, {"accept", "application/yang-data+json"}}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "contact": "contact",
    "hostname": "hostname",
    "location": "location"
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"accept", "blabla"}}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "contact": "contact",
    "hostname": "hostname",
    "location": "location"
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"accept", "*/*"}}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "contact": "contact",
    "hostname": "hostname",
    "location": "location"
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"accept", "application/*"}}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "contact": "contact",
    "hostname": "hostname",
    "location": "location"
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"accept", "image/*"}}) == Response{406, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "operation-not-supported",
        "error-message": "No requested format supported"
      }
    ]
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"content-type", "application/*"}}) == Response{415, jsonHeaders, R"({
  "ietf-restconf:errors": {
    "error": [
      {
        "error-type": "application",
        "error-tag": "operation-not-supported",
        "error-message": "content-type format value not supported"
      }
    ]
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {{"accept", "application/yang-data+json;q=0.4,application/yang-data+xml"}}) == Response{200, xmlHeaders, R"(<system xmlns="urn:ietf:params:xml:ns:yang:ietf-system">
  <contact>contact</contact>
  <hostname>hostname</hostname>
  <location>location</location>
</system>
)"});
    }

    SECTION("NMDA (RFC 8527)")
    {
        srSess.switchDatastore(sysrepo::Datastore::Startup);
        srSess.setItem("/ietf-system:system/contact", "startup-contact");
        srSess.applyChanges();

        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-system:system", {}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "contact": "contact",
    "hostname": "hostname",
    "location": "location"
  }
}
)"});

        REQUIRE(get(RESTCONF_ROOT_DS("startup") "/ietf-system:system", {}) == Response{200, jsonHeaders, R"({
  "ietf-system:system": {
    "contact": "startup-contact"
  }
}
)"});
    }

    SECTION("yang-library-version")
    {
        REQUIRE(get(RESTCONF_ROOT "/yang-library-version", {}) == Response{200, jsonHeaders, R"({
  "ietf-restconf:yang-library-version": "2019-01-04"
}
)"});

        REQUIRE(get(RESTCONF_ROOT "/yang-library-version", {{"accept", "application/yang-data+xml"}}) == Response{200, xmlHeaders,
                R"(<yang-library-version xmlns="urn:ietf:params:xml:ns:yang:ietf-restconf">2019-01-04</yang-library-version>
)"});
    }

    SECTION("restconf monitoring")
    {
        REQUIRE(get(RESTCONF_DATA_ROOT "/ietf-restconf-monitoring:restconf-state", {}) == Response{200, jsonHeaders, R"({
  "ietf-restconf-monitoring:restconf-state": {
    "capabilities": {
      "capability": [
        "urn:ietf:params:restconf:capability:defaults:1.0?basic-mode=explicit",
        "urn:ietf:params:restconf:capability:depth:1.0",
        "urn:ietf:params:restconf:capability:with-defaults:1.0"
      ]
    }
  }
}
)"});

    }

    SECTION("with-defaults")
    {
        SECTION("Implicit default node")
        {

            REQUIRE(get(RESTCONF_DATA_ROOT "/example:a?with-defaults=report-all", {}) == Response{200, jsonHeaders, R"({
  "example:a": {
    "b": {
      "c": {
        "enabled": true
      }
    },
    "example-augment:b": {
      "c": {
        "enabled": true
      }
    }
  }
}
)"});
            REQUIRE(get(RESTCONF_DATA_ROOT "/example:a?with-defaults=explicit", {}) == Response{200, jsonHeaders, R"({

}
)"});
            REQUIRE(get(RESTCONF_DATA_ROOT "/example:a?with-defaults=trim", {}) == Response{200, jsonHeaders, R"({

}
)"});
            REQUIRE(get(RESTCONF_DATA_ROOT "/example:a?with-defaults=report-all-tagged", {}) == Response{200, jsonHeaders, R"({
  "example:a": {
    "b": {
      "c": {
        "enabled": true,
        "@enabled": {
          "ietf-netconf-with-defaults:default": true
        }
      }
    },
    "example-augment:b": {
      "c": {
        "enabled": true,
        "@enabled": {
          "ietf-netconf-with-defaults:default": true
        }
      }
    }
  }
}
)"});
        }

        SECTION("Explicit default node")
        {
            srSess.switchDatastore(sysrepo::Datastore::Running);
            srSess.setItem("/example:a/b/c/enabled", "true");
            srSess.applyChanges();

            REQUIRE(get(RESTCONF_DATA_ROOT "/example:a?with-defaults=report-all", {}) == Response{200, jsonHeaders, R"({
  "example:a": {
    "b": {
      "c": {
        "enabled": true
      }
    },
    "example-augment:b": {
      "c": {
        "enabled": true
      }
    }
  }
}
)"});

            REQUIRE(get(RESTCONF_DATA_ROOT "/example:a?with-defaults=explicit", {}) == Response{200, jsonHeaders, R"({
  "example:a": {
    "b": {
      "c": {
        "enabled": true
      }
    }
  }
}
)"});

            REQUIRE(get(RESTCONF_DATA_ROOT "/example:a?with-defaults=trim", {}) == Response{200, jsonHeaders, R"({

}
)"});

            REQUIRE(get(RESTCONF_DATA_ROOT "/example:a?with-defaults=report-all-tagged", {}) == Response{200, jsonHeaders, R"({
  "example:a": {
    "b": {
      "c": {
        "enabled": true,
        "@enabled": {
          "ietf-netconf-with-defaults:default": true
        }
      }
    },
    "example-augment:b": {
      "c": {
        "enabled": true,
        "@enabled": {
          "ietf-netconf-with-defaults:default": true
        }
      }
    }
  }
}
)"});
        }
    }

    SECTION("Implicit node with default value")
    {
        // RFC 4080, 3.5.4: If target of the query is implicitly created node with default value, ignore basic mode
        REQUIRE(get(RESTCONF_DATA_ROOT "/example:a/b/c/enabled", {}) == Response{200, jsonHeaders, R"({
  "example:a": {
    "b": {
      "c": {
        "enabled": true
      }
    }
  }
}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/example:a/b/c/enabled?with-defaults=explicit", {}) == Response{200, jsonHeaders, R"({

}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/example:a/b/c/enabled?with-defaults=trim", {}) == Response{200, jsonHeaders, R"({

}
)"});

        REQUIRE(get(RESTCONF_DATA_ROOT "/example:a/b/c/enabled?with-defaults=report-all", {}) == Response{200, jsonHeaders, R"({
  "example:a": {
    "b": {
      "c": {
        "enabled": true
      }
    }
  }
}
)"});
        REQUIRE(get(RESTCONF_DATA_ROOT "/example:a/b/c/enabled?with-defaults=report-all-tagged", {}) == Response{200, jsonHeaders, R"({
  "example:a": {
    "b": {
      "c": {
        "enabled": true,
        "@enabled": {
          "ietf-netconf-with-defaults:default": true
        }
      }
    }
  }
}
)"});
    }
}
