#include "nexus/types.h"

const char* nexus::errString(Err e) {
    switch (e) {
        case Err::Ok:               return "ok";
        case Err::IoError:          return "io error";
        case Err::MountFailed:      return "mount failed";
        case Err::InvalidArg:       return "invalid argument";
        case Err::Unauthorized:     return "unauthorized";
        case Err::Timeout:          return "timeout";
        case Err::NotFound:         return "not found";
        case Err::AlreadyExists:    return "already exists";
        case Err::CapabilityDenied: return "capability denied";
        case Err::ScriptFailed:     return "script failed";
        case Err::ProtocolError:    return "protocol error";
        case Err::Unsupported:      return "unsupported";
    }
    return "unknown";
}

const char* nexus::rootProviderName(RootProvider p) {
    switch (p) {
        case RootProvider::None:     return "none";
        case RootProvider::Magisk:   return "magisk";
        case RootProvider::KernelSU: return "kernelsu";
        case RootProvider::APatch:   return "apatch";
    }
    return "unknown";
}
