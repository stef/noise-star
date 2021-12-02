#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <IKpsk2.h>

typedef Noise_IKpsk2_device_t        device;
typedef Noise_IKpsk2_session_t       session;
typedef Noise_IKpsk2_peer_t          peer;
typedef Noise_IKpsk2_encap_message_t encap_message;
typedef Noise_IKpsk2_rcode           rcode;
typedef uint32_t              peer_id;

// Using IKpsk2 with:
// - DH: Curve25519
// - AEAD: ChaChaPoly
// - Hash: SHA256
#define AEAD_KEY_SIZE 32
#define DH_KEY_SIZE   32
#define HASH_SIZE     32
#define PSK_SIZE      32

#define RETURN_IF_ERROR(e, msg) if (!(e)) { printf("Error: %s\n", msg); return 1; }

// Symmetric key used by Alice for serialization/deserialization
uint8_t alice_srlz_key[AEAD_KEY_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Alice's DH keys
uint8_t alice_spriv[DH_KEY_SIZE] = {
    0xc3, 0xda, 0x55, 0x37, 0x9d, 0xe9, 0xc6, 0x90,
    0x8e, 0x94, 0xea, 0x4d, 0xf2, 0x8d, 0x08, 0x4f,
    0x32, 0xec, 0xcf, 0x03, 0x49, 0x1c, 0x71, 0xf7,
    0x54, 0xb4, 0x07, 0x55, 0x77, 0xa2, 0x85, 0x52
};

uint8_t alice_spub[DH_KEY_SIZE]  = { 0 };

// Symmetric key used by Bob for serialization/deserialization
uint8_t bob_srlz_key[AEAD_KEY_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Bob's DH keys
uint8_t bob_spriv[DH_KEY_SIZE] = {
    0xc3, 0xda, 0x55, 0x37, 0x9d, 0xe9, 0xc6, 0x90,
    0x8e, 0x94, 0xea, 0x4d, 0xf2, 0x8d, 0x08, 0x4f,
    0x32, 0xec, 0xcf, 0x03, 0x49, 0x1c, 0x71, 0xf7,
    0x54, 0xb4, 0x07, 0x55, 0x77, 0xa2, 0x85, 0x52
};


uint8_t bob_spub[DH_KEY_SIZE]  = { 0 };

// Pre-shared key
uint8_t psk[PSK_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

int main () {
    uint8_t prologue[10] = "Noise* 1.0";
    rcode res;
    encap_message *encap_msg;
    uint32_t cipher_msg_len;
    uint8_t *cipher_msg;
    uint32_t plain_msg_len;
    uint8_t *plain_msg;

    // Generate the public keys from the private keys
    Noise_IKpsk2_dh_secret_to_public(alice_spub, alice_spriv);
    Noise_IKpsk2_dh_secret_to_public(bob_spub, bob_spriv);

    /*
     * Initialize Alice's device
     */
    // Create the device
    device *alice_device = Noise_IKpsk2_device_create(10, prologue, (uint8_t*) "Alice",
                                               alice_srlz_key, alice_spriv);
    
    // Register Bob
    peer *peer_bob = Noise_IKpsk2_device_add_peer(alice_device, (uint8_t*) "Bob", bob_spub, psk);
    if (!peer_bob) return 1;
    peer_id bob_id = Noise_IKpsk2_peer_get_id(peer_bob);

    /*
     * Initialize Bob's device
     */
    // Create the device
    device *bob_device = Noise_IKpsk2_device_create(10, prologue, (uint8_t*) "Bob",
                                             bob_srlz_key, bob_spriv);

    // Register Alice
    // Note that we parameterized the code generation so that whenever we receive
    // a public static key during the handshake, this static key is accepted only
    // if it was registered before. If we don't register Alice, the first message
    // from Alice to Bob will get rejected.
    // Note that it is possible to parameterize the API to accept unknown remote
    // keys, but in that case it is necessary to provide a validation function
    // to check remote static keys upon receiving them (the payload should then
    // contain a certificate for the key): we can't use arbitrary remote static
    // public keys.
    peer *peer_alice = Noise_IKpsk2_device_add_peer(bob_device, (uint8_t*) "Alice", alice_spub, psk);
    if (!peer_alice) return 1;

    /*
     * Start communicating
     */
    // We first need to create communication sessions.
    // Alice being the initiator, she needs to know who to send the first
    // message, hence the bob_id parameter.
    session *alice_session = Noise_IKpsk2_session_create_initiator(alice_device, bob_id);
    RETURN_IF_ERROR(alice_session, "Alice session creation");

    // Bob, however, uses the first message to learn Alice's identity.
    session *bob_session = Noise_IKpsk2_session_create_responder(bob_device);
    RETURN_IF_ERROR(alice_session, "Bob session creation");
      
    // # Step 1: Send an empty message from Alice to Bob

    // ## Alice: generate the message

    // Before sending a message, we need to pack it with a confidentiality
    // level. Upon sending the message, if the message is non-empty, the
    // session state will dynamically check that it can provide the requested
    // security guarantees, and will fail otherwise.
    // Here, the message is empty, so no check is actually performed, but we
    // still request the lowest guarantees (no guarantees, actually: the message
    // is considered public).
    encap_msg = Noise_IKpsk2_pack_message_with_conf_level(NOISE_IKPSK2_CONF_ZERO, 0, NULL);
    res = Noise_IKpsk2_session_write(encap_msg, alice_session, &cipher_msg_len, &cipher_msg);
    RETURN_IF_ERROR(Noise_IKpsk2_rcode_is_success(res), "Send message 0");
    Noise_IKpsk2_encap_message_p_free(encap_msg);

    // ## Bob: read the message
    res = Noise_IKpsk2_session_read(&encap_msg, bob_session, cipher_msg_len, cipher_msg);
    RETURN_IF_ERROR(Noise_IKpsk2_rcode_is_success(res), "Receive message 0");

    // In order to actually read the message, Bob needs to unpack it.
    // Unpacking is similar to packing, but here we use an authentication level:
    // if at the current step the message is non-empty and the protocol doesn't
    // provide enough authentication guarantees, then the unpacking fails.
    RETURN_IF_ERROR(
                    Noise_IKpsk2_unpack_message_with_auth_level(&plain_msg_len, &plain_msg,
                                                         NOISE_IKPSK2_AUTH_ZERO, encap_msg),
                    "Unpack message 0");
    Noise_IKpsk2_encap_message_p_free(encap_msg);
    if (cipher_msg_len > 0) free(cipher_msg);
    if (plain_msg_len > 0) free(plain_msg);
    
    // # Step 2: Send an empty message from Bob to Alice.
    // Very similar to step 1.

    // ## Bob: generate the message
    encap_msg = Noise_IKpsk2_pack_message_with_conf_level(NOISE_IKPSK2_CONF_ZERO, 0, NULL);
    res = Noise_IKpsk2_session_write(encap_msg, bob_session, &cipher_msg_len, &cipher_msg);
    RETURN_IF_ERROR(Noise_IKpsk2_rcode_is_success(res), "Send message 1");
    Noise_IKpsk2_encap_message_p_free(encap_msg);

    // ## Alice: read the message
    res = Noise_IKpsk2_session_read(&encap_msg, alice_session, cipher_msg_len, cipher_msg);
    RETURN_IF_ERROR(Noise_IKpsk2_rcode_is_success(res), "Receive message 1");
    RETURN_IF_ERROR(
                    Noise_IKpsk2_unpack_message_with_auth_level(&plain_msg_len, &plain_msg,
                                                         NOISE_IKPSK2_AUTH_ZERO, encap_msg),
                    "Unpack message 1");
    Noise_IKpsk2_encap_message_p_free(encap_msg);
    if (cipher_msg_len > 0) free(cipher_msg);
    if (plain_msg_len > 0) free(plain_msg);

    
    // # Step 3 : Send a confidential message from Alice to Bob

    // By now, Alice should have reached the best security level.
    // Send a secret message, and request the highest confidentiality
    // guarantees.

    // ## Alice: generate the message
    // We request strong forward secrecy
    encap_msg = Noise_IKpsk2_pack_message_with_conf_level(NOISE_IKPSK2_CONF_STRONG_FORWARD_SECRECY,
        11, (uint8_t*) "Hello Bob!");
    res = Noise_IKpsk2_session_write(encap_msg, alice_session, &cipher_msg_len, &cipher_msg);
    RETURN_IF_ERROR(Noise_IKpsk2_rcode_is_success(res), "Send message 2");
    Noise_IKpsk2_encap_message_p_free(encap_msg);

    // ## Bob: read the message
    // We request the highest authentication guarantees (known sender, no KCI).
    res = Noise_IKpsk2_session_read(&encap_msg, bob_session, cipher_msg_len, cipher_msg);
    RETURN_IF_ERROR(Noise_IKpsk2_rcode_is_success(res), "Receive message 2");
    RETURN_IF_ERROR(
                    Noise_IKpsk2_unpack_message_with_auth_level(&plain_msg_len, &plain_msg,
                                                         NOISE_IKPSK2_AUTH_KNOWN_SENDER_NO_KCI,
                                                         encap_msg),
                    "Unpack message 2");
    Noise_IKpsk2_encap_message_p_free(encap_msg);
    if (cipher_msg_len > 0) free(cipher_msg);
    if (plain_msg_len > 0) free(plain_msg);

    // # Step 4: Send a confidential message from Bob to Alice.
    // Same for Bob: we can now send messages with the highest confidentiality level.

    // ## Bob: generate the message (request max confidentiality)
    encap_msg = Noise_IKpsk2_pack_message_with_conf_level(NOISE_IKPSK2_CONF_STRONG_FORWARD_SECRECY,
        11, (uint8_t*) "Hello Alice!");
    res = Noise_IKpsk2_session_write(encap_msg, bob_session, &cipher_msg_len, &cipher_msg);
    RETURN_IF_ERROR(Noise_IKpsk2_rcode_is_success(res), "Send message 3");
    Noise_IKpsk2_encap_message_p_free(encap_msg);

   // ## Alice: read the message (request max authentication)
    res = Noise_IKpsk2_session_read(&encap_msg, alice_session, cipher_msg_len, cipher_msg);
    RETURN_IF_ERROR(Noise_IKpsk2_rcode_is_success(res), "Receive message 3");
    RETURN_IF_ERROR(
                    Noise_IKpsk2_unpack_message_with_auth_level(&plain_msg_len, &plain_msg,
                                                         NOISE_IKPSK2_AUTH_KNOWN_SENDER_NO_KCI,
                                                         encap_msg),
                    "Unpack message 3");
    Noise_IKpsk2_encap_message_p_free(encap_msg);
    if (cipher_msg_len > 0) free(cipher_msg);
    if (plain_msg_len > 0) free(plain_msg);

    /*
     * Cleanup
     */
    // Free the sessions, then the devices
    Noise_IKpsk2_session_free(alice_session);
    Noise_IKpsk2_session_free(bob_session);
    Noise_IKpsk2_device_free(alice_device);
    Noise_IKpsk2_device_free(bob_device);

    
    printf("Success!\n");

    return 0;
}
