/* kd_mldsa_tool.c — minimal ML-DSA-65 (FIPS 204, post-quantum) CLI around
 * liboqs, used on BOTH sides of the hybrid signing pipeline:
 *   - built static for x86_64-linux: runs on the cPanel server (via PHP
 *     shell_exec, same pattern as the Ed448 CLI openssl calls) to SIGN
 *     each response.
 *   - built static for x86_64-linux and run once here (in this session,
 *     also x86_64 Linux) to GENERATE the one deployment keypair.
 *   - the Android arm64 side never runs this tool; it links liboqs
 *     directly (see login.h kd_verify_mldsa65()) and never needs a
 *     private key at all.
 *
 * Deliberately tiny and dependency-free beyond liboqs + libc, so a fully
 * static build has nothing else to go wrong at runtime on a shared host.
 *
 * usage:
 *   kd_mldsa_tool keygen <privout> <pubout>
 *       Generates a fresh ML-DSA-65 keypair. Writes RAW bytes (not hex,
 *       not PEM — liboqs doesn't speak those formats) to both files.
 *   kd_mldsa_tool sign <privfile> <msgfile> <sigfile>
 *       Signs the exact bytes of <msgfile> with the raw private key in
 *       <privfile>, writes the raw signature to <sigfile>. ML-DSA
 *       signatures are variable length (up to length_signature) —
 *       <sigfile>'s size on disk after a successful run IS the real
 *       signature length, no separate length field needed.
 */
#include <oqs/oqs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *path, unsigned char **buf, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    unsigned char *b = malloc((size_t)sz > 0 ? (size_t)sz : 1);
    if (!b) { fclose(f); return -1; }
    size_t n = sz > 0 ? fread(b, 1, (size_t)sz, f) : 0;
    fclose(f);
    if (n != (size_t)sz) { free(b); return -1; }
    *buf = b;
    *len = (size_t)sz;
    return 0;
}

static int write_file(const char *path, const unsigned char *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(buf, 1, len, f);
    int ok = (fclose(f) == 0) && (n == len);
    return ok ? 0 : -1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage:\n"
            "  %s keygen <privout> <pubout>\n"
            "  %s sign <privfile> <msgfile> <sigfile>\n",
            argv[0], argv[0]);
        return 2;
    }

    if (OQS_SIG_alg_is_enabled(OQS_SIG_alg_ml_dsa_65) != 1) {
        fprintf(stderr, "ML-DSA-65 is not enabled in this liboqs build\n");
        return 1;
    }

    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_65);
    if (!sig) { fprintf(stderr, "OQS_SIG_new failed\n"); return 1; }

    int rc = 1;

    if (strcmp(argv[1], "keygen") == 0 && argc == 4) {
        unsigned char *pub  = malloc(sig->length_public_key);
        unsigned char *priv = malloc(sig->length_secret_key);
        if (!pub || !priv) { fprintf(stderr, "oom\n"); goto done; }

        if (OQS_SIG_keypair(sig, pub, priv) != OQS_SUCCESS) {
            fprintf(stderr, "keypair generation failed\n");
            goto done;
        }
        if (write_file(argv[2], priv, sig->length_secret_key) != 0) {
            fprintf(stderr, "failed writing private key to %s\n", argv[2]);
            goto done;
        }
        if (write_file(argv[3], pub, sig->length_public_key) != 0) {
            fprintf(stderr, "failed writing public key to %s\n", argv[3]);
            goto done;
        }
        fprintf(stderr, "OK: priv=%zu bytes, pub=%zu bytes, alg=%s\n",
                sig->length_secret_key, sig->length_public_key, sig->method_name);
        OQS_MEM_secure_free(priv, sig->length_secret_key);
        free(pub);
        rc = 0;
        goto done;
    }

    if (strcmp(argv[1], "sign") == 0 && argc == 5) {
        unsigned char *priv = NULL, *msg = NULL, *sigbuf = NULL;
        size_t privlen = 0, msglen = 0, siglen = 0;

        if (read_file(argv[2], &priv, &privlen) != 0 || privlen != sig->length_secret_key) {
            fprintf(stderr, "bad/missing private key file %s (expected %zu bytes, got %zu)\n",
                    argv[2], sig->length_secret_key, privlen);
            goto done;
        }
        if (read_file(argv[3], &msg, &msglen) != 0) {
            fprintf(stderr, "bad/missing message file %s\n", argv[3]);
            goto done;
        }
        sigbuf = malloc(sig->length_signature);
        if (!sigbuf) { fprintf(stderr, "oom\n"); goto done; }

        if (OQS_SIG_sign(sig, sigbuf, &siglen, msg, msglen, priv) != OQS_SUCCESS) {
            fprintf(stderr, "signing failed\n");
            goto done;
        }
        if (write_file(argv[4], sigbuf, siglen) != 0) {
            fprintf(stderr, "failed writing signature to %s\n", argv[4]);
            goto done;
        }
        rc = 0;
        if (priv) OQS_MEM_secure_free(priv, privlen);
        free(msg);
        free(sigbuf);
        goto done;
    }

    fprintf(stderr, "unrecognized command/arg count: %s\n", argv[1]);

done:
    OQS_SIG_free(sig);
    return rc;
}
