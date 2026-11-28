# omvll_config.py — O-MVLL obfuscation config
# Goes in the REPO ROOT. The CI workflow sets OMVLL_CONFIG to this file.
#
# Only the authentication-critical functions are obfuscated. Obfuscating
# EVERYTHING would bloat the binary and slow the build a lot for no benefit
# (ImGui, Vulkan, etc. don't need it). The list below targets the exact
# functions a cracker would attack: the license decision, the crypto, the
# integrity/anti-tamper gates.
#
# If after the first build some function is NOT obfuscated, check the CI log
# (O-MVLL prints what it processes) and adjust the names here — C++ names get
# mangled, so matching is done against both the mangled and demangled name.

import omvll
from functools import cache

# Substrings matched against each function's (demangled) name.
# Expanded pass: every function that touches the key, the crypto, the
# session, or anti-tamper state — not just the entry points. Flattening
# only perform_auth() left its callees (kd_open_secret, kd_seal_request,
# etc.) readable in Ghidra, which still reconstructs most of the flow from
# the untouched half. Obfuscating the whole call chain removes that.
AUTH_FUNCS = [
    # entry points
    "perform_auth", "perform_login", "perform_hwid_auth",
    "run_silent", "main",
    # kd_ctl's own process/watcher logic — same obfuscation bar as the
    # auth path it shares a binary with.
    "kd_inx_start", "kd_inx_stop", "kd_inx_running",
    "kd_watch_loop", "kd_watch_start", "kd_watch_stop_impl",
    "kd_foreground_package", "kd_list_apps", "kd_app_label",
    # redundant auth-state (the actual value a patch would target)
    "kd_get_kernel_ok", "kd_set_kernel_ok", "kd_auth_cookie_init",
    # integrity / anti-tamper gate + the new self-debug lock + emulator check
    "ix_integrity_gate", "ix_tamper_detected", "ix_ic_die",
    "ix_dbg_tracerpid", "ix_frida_present", "ix_frida_in_maps",
    "ix_frida_thread_names", "ix_frida_default_port", "ix_hookfw_present",
    "ix_fn_hooked", "ix_self_debug_lock", "ix_emulator_detected",
    "ix_ic_url_sane", "ix_ic_pub_sane",
    # crypto / transport (the wire format a cracker reverses to forge requests)
    "kd_open_secret", "kd_verify_ed25519", "kd_verify_ed448", "kd_verify_mldsa65", "kd_open_response",
    "kd_seal_request", "kd_auth_post", "kd_derive_subkeys",
    "kd_client_integrity_matches", "kd_wipe_server_secret",
    "kd_hex_decode_any", "kd_hex_decode",
    # local key storage (what an attacker dumps to clone a license)
    "kd_load_or_create_master_key", "kd_save_key", "kd_key_format_ok",
    "kd_sha256_file",
]

def _is_target(func) -> bool:
    name = (getattr(func, "name", "") or "")
    dem  = (getattr(func, "demangled_name", "") or "")
    hay  = name + "||" + dem
    return any(t in hay for t in AUTH_FUNCS)

# [CRASH FIX 2026-11-08, revised] First cut (excluding only run_silent) was
# aimed wrong: the WebUI always calls `inx_login_@api <key>`, so run_silent's
# keyNameIn is NEVER empty at runtime -- the kd_load_saved_key() branch it
# guards is dead code on this path and was never the issue.
#
# Device-confirmed (webroot/script.js's loginKeyButton handler, login.h):
# login succeeds the FIRST time a key is entered, then SIGBUS on every call
# after that -- same binary, same argv-with-key call every time. The one
# real branch that differs between "first call ever" and "every call after"
# is inside kd_load_or_create_master_key(): the master-key file does not
# exist yet on the first call (random bytes generated, file written) and
# DOES exist on every later call (file opened and read instead). kd_save_key()
# -- called after every successful login when a key was passed on argv --
# calls kd_load_or_create_master_key(), so this runs on every WebUI login.
#
# main()/run_silent()/perform_login()/kd_save_key()/kd_load_or_create_master_key()
# /kd_load_saved_key()/kd_mix_master_key() are all thin `static inline`
# wrappers in login.h, small enough that the compiler may inline several of
# them into one physical function before O-MVLL ever sees it -- so excluding
# only one name by itself may silently do nothing if its code ended up
# living inside a different function's body. All of them are excluded here,
# by name, so the fix holds regardless of which way the inliner merged them.
# (Tombstone PC 0x20b468 is confirmed INSIDE main()'s own flattened dispatch
# -- it is the instruction right before __libc_init's call into main -- so
# main's dispatch is excluded for certain, not just suspected.)
#
# Deliberately NOT excluded, still full flatten_cfg / break_control_flow /
# anti_hooking: perform_auth (the actual license decision + network),
# kd_open_secret, kd_seal_request, kd_derive_subkeys, kd_verify_ed25519/
# ed448/mldsa65, kd_auth_post, kd_client_integrity_matches,
# kd_wipe_server_secret, ix_integrity_gate and every anti-debug/anti-hook
# detector. That is the half of the binary a cracker actually wants to read
# -- the wire protocol and the signature checks -- and it is untouched.
_FLATTEN_EXCLUDE = (
    "main", "run_silent", "perform_login",
    "kd_save_key", "kd_load_or_create_master_key",
    "kd_load_saved_key", "kd_mix_master_key",
)

def _is_flatten_target(func) -> bool:
    name = (getattr(func, "name", "") or "")
    dem  = (getattr(func, "demangled_name", "") or "")
    hay  = name + "||" + dem
    if any(t in hay for t in _FLATTEN_EXCLUDE):
        return False
    return _is_target(func)

class KdConfig(omvll.ObfuscationConfig):
    def __init__(self):
        super().__init__()

    # Control-flow flattening: the single most effective pass — turns the
    # function's clean if/else graph into a dispatch loop that is very hard
    # to follow in Ghidra.
    def flatten_cfg(self, mod, func):
        return _is_flatten_target(func)

    # Insert bogus-but-equivalent control flow (opaque branches).
    def break_control_flow(self, mod, func):
        return _is_flatten_target(func)

    # Replace integer constants with opaque computations, so the auth
    # magic values / flags aren't visible as plain immediates.
    def obfuscate_constants(self, mod, func):
        return _is_target(func)

    # O-MVLL's own anti-hooking insertion on the hottest function.
    def anti_hooking(self, mod, func):
        return _is_flatten_target(func)

    # Extra string encoding INSIDE the auth functions only (oxorany already
    # covers strings globally; this is a second layer where it matters).
    def obfuscate_string(self, mod, func, string):
        return _is_target(func)

@cache
def omvll_get_config() -> omvll.ObfuscationConfig:
    return KdConfig()
