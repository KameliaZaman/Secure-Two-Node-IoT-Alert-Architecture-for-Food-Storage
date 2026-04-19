ASCON_KEY_SIZE = 16
ASCON_NONCE_SIZE = 16
ASCON_TAG_SIZE = 16

def simple_round(state):
    for i in range(32):
        b = state[i] ^ 0xAA
        state[i] = ((b << 1) & 0xFF) | (b >> 7)


def ascon128_encrypt(ciphertext, plaintext, key, nonce):

    state = [0] * 32
    for i in range(16):
        state[i] = key[i]
    for i in range(16):
        state[16 + i] = nonce[i]

    for _ in range(6):
        simple_round(state)

    for i in range(len(plaintext)):
        ct_byte = plaintext[i] ^ state[i % 32]
        ciphertext.append(ct_byte)

        state[i % 32] ^= plaintext[i]

    for i in range(16):
        ciphertext.append(state[i])

    return len(ciphertext)


def ascon128_decrypt(plaintext, ciphertext, key, nonce):
    ct_len = len(ciphertext)
    if ct_len < 16:
        return -1

    msg_len = ct_len - 16
    state = [0] * 32
    for i in range(16):
        state[i] = key[i]
    for i in range(16):
        state[16 + i] = nonce[i]

    for _ in range(6):
        simple_round(state)

    for i in range(msg_len):
        pt_byte = ciphertext[i] ^ state[i % 32]
        plaintext.append(pt_byte)
        state[i % 32] ^= pt_byte

    tag = ciphertext[msg_len:]
    for i in range(16):
        if tag[i] != state[i]:
            return -1

    return msg_len