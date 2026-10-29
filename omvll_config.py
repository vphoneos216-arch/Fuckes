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

class KdConfig(omvll.ObfuscationConfig):
    def __init__(self):
        super().__init__()

    # Control-flow flattening: the single most effective pass — turns the
    # function's clean if/else graph into a dispatch loop that is very hard
    # to follow in Ghidra.
    def flatten_cfg(self, mod, func):
        return _is_target(func)

    # Insert bogus-but-equivalent control flow (opaque branches).
    def break_control_flow(self, mod, func):
        return _is_target(func)

    # Replace integer constants with opaque computations, so the auth
    # magic values / flags aren't visible as plain immediates.
    def obfuscate_constants(self, mod, func):
        return _is_target(func)

    # O-MVLL's own anti-hooking insertion on the hottest function.
    def anti_hooking(self, mod, func):
        return _is_target(func)

    # Extra string encoding INSIDE the auth functions only (oxorany already
    # covers strings globally; this is a second layer where it matters).
    def obfuscate_string(self, mod, func, string):
        return _is_target(func)

@cache
def omvll_get_config() -> omvll.ObfuscationConfig:
    return KdConfig()
