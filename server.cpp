#include "commonlib/net_wrapper.h"
#include "commonlib/messages.h"
#include "commonlib/commonlib.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/err.h>

my_buffer my_buff;

uint64_t sr_nonce;
uint64_t cl_nonce;

uint64_t cl_seq_num;
uint64_t sr_seq_num;

unsigned char session_key[16];


bool send_hello_msg(int sock) {
	hello_msg h;
	h.t = SERVER_HELLO;
	h.nonce = sr_nonce = sr_seq_num = generate_nonce();
	convert_to_network_order(&h);
	printf("server sends nonce: %ld\n",sr_nonce);
	if( send_data(sock,(unsigned char*)&h, sizeof(h)) == sizeof(h) )
		return true;
	else
		return false;

}

bool recv_hello_msg(int sd){
	hello_msg h_msg;
	if(	!recv_msg(sd, &h_msg ,CLIENT_HELLO) )
	{
		printf("Error receive CLIENT_HELLO\n");
		return false;
	} else  {
		cl_nonce = cl_seq_num = h_msg.nonce;
		return true;
	}
}

int analyze_message(unsigned char* buf)
{
	convert_to_host_order(buf);
 	switch( ((simple_msg*)buf)->t ) {
  		case CLIENT_HELLO:
  			cl_nonce = cl_seq_num =((hello_msg*)buf)->nonce;
  			printf("Client nonce received: %ld\n",cl_nonce);
  			break;
		default:
			return -2;
	}

	return 0;
}

bool send_server_verification(int cl_sd)
{
	SignatureMaker sm("keys/rsa_server_privkey.pem");

	unsigned char to_sign[16];
	memcpy(to_sign, &cl_nonce, 8);
	memcpy(to_sign + 8, &sr_nonce, 8);
	sm.sign(to_sign, 16);

	unsigned char *signature;
	unsigned int signature_len = sm.sign_end(&signature);

	// send client_nonce|server_noncce
	send_data(cl_sd, signature, signature_len);
	printf("sent: signature_len  = %u\n", signature_len);

	return true;
}

bool check_client_identity(int cl_sd)
{
	// 4) Validate Client and Get Session key
	// getting session encrypted key
	unsigned int auth_encrypted_key_len = recv_data(cl_sd, &my_buff);
	if(auth_encrypted_key_len > 10000)
	{
		printf("error: auth_encrypted_key_len = %u invalid size!\n", auth_encrypted_key_len);
		return false;
	}
	unsigned char *auth_encrypted_key = new unsigned char[auth_encrypted_key_len];
	memcpy(auth_encrypted_key, my_buff.buf, auth_encrypted_key_len);

	// getting session iv
	unsigned char *auth_iv = new unsigned char[EVP_CIPHER_iv_length(EVP_aes_128_cbc())];
	unsigned int auth_iv_len = recv_data(cl_sd, &my_buff);
	if((int)auth_iv_len != EVP_CIPHER_iv_length(EVP_aes_128_cbc())) {
		printf("error: auth_iv_len = %u instead of %d!\n", auth_iv_len, EVP_CIPHER_iv_length(EVP_aes_128_cbc()));
		return false;
	}
	memcpy(auth_iv, my_buff.buf, auth_iv_len);


	// getting client authentication header
	client_auth auth_header_msg;
	recv_data(cl_sd, &my_buff);
	memcpy(&auth_header_msg, my_buff.buf, sizeof(auth_header_msg));
	convert_to_host_order(&auth_header_msg);

	printf("client header: ciphertext_len = %u, username_length = %u, password_length = %u\n",
		auth_header_msg.total_ciphertext_size, auth_header_msg.username_length, auth_header_msg.password_length);

	DecryptSession asymm_authclient_decipher("keys/rsa_server_privkey.pem", auth_encrypted_key, auth_encrypted_key_len, auth_iv);

	// receive ciphertext
	unsigned int auth_cipherlen = recv_data(cl_sd, &my_buff);

	// decode ciphertext
	unsigned char *auth_plaintext = new unsigned char[auth_header_msg.total_ciphertext_size];
	unsigned int auth_plainlen = 0;
	asymm_authclient_decipher.decrypt(my_buff.buf, auth_cipherlen);
	asymm_authclient_decipher.decrypt_end();
	auth_plainlen = asymm_authclient_decipher.flush_plaintext(&auth_plaintext);

	// decompose plaintext
	unsigned int pl_offset = 0;

	uint64_t received_server_nonce;
	memcpy(&received_server_nonce, auth_plaintext, 8);
	pl_offset += 8;
	printf("received_server_nonce = %ld\n", received_server_nonce);

	memcpy(session_key, auth_plaintext + pl_offset, 16);
	pl_offset += 16;

	unsigned char *received_username = new unsigned char[auth_header_msg.username_length];
	memcpy(received_username, auth_plaintext + pl_offset, auth_header_msg.username_length);
	pl_offset += auth_header_msg.username_length;

	unsigned char *received_password = new unsigned char[auth_header_msg.password_length];
	memcpy(received_password, auth_plaintext + pl_offset, auth_header_msg.password_length);
	pl_offset += auth_header_msg.username_length;

	printf("got key:\n");
	print_hex(session_key, 16);
	printf("got: username = %s, password = %s\n", received_username, received_password);

	// 5) send auth result
	simple_msg auth_resp_msg;

	if(received_server_nonce != sr_nonce)
	{
		printf("error: nonces unmatch!\n");
		auth_resp_msg.t = AUTHENTICATION_FAILED;
		send_data(cl_sd, (unsigned char*)&auth_resp_msg, sizeof(auth_resp_msg));
		return false;
	}

	auth_resp_msg.t = AUTHENTICATION_OK;
	send_data(cl_sd, (unsigned char*)&auth_resp_msg, sizeof(auth_resp_msg));

	return true;
}

bool receive_command(int cl_sd, unsigned char **received_command, unsigned int* received_command_len)
{
	// getting iv
	unsigned char *command_iv = new unsigned char[EVP_CIPHER_iv_length(EVP_aes_128_cbc())];
	unsigned int command_iv_len = recv_data(cl_sd, &my_buff);
	memcpy(command_iv, my_buff.buf, command_iv_len);

	// getting {seqnum|command_str}_Ksess
	unsigned int command_ciphertext_len = recv_data(cl_sd, &my_buff);
	unsigned char *command_ciphertext = new unsigned char[command_ciphertext_len];
	memcpy(command_ciphertext, my_buff.buf, command_ciphertext_len);

	// getting HMAC_Ksess{seqnum|command_str}_Ksess
	unsigned int command_hmac_len = recv_data(cl_sd, &my_buff);
	unsigned char *command_hmac = new unsigned char[command_hmac_len];
	memcpy(command_hmac, my_buff.buf, command_hmac_len);

	// making HMAC_Ksess{seqnum|command_str}_Ksess
	unsigned char *computed_hmac;
	HMACMaker hm(session_key, 16);
	hm.hash(command_ciphertext, command_ciphertext_len);
	hm.hash_end(&computed_hmac);

	if(CRYPTO_memcmp(computed_hmac, command_hmac, HMAC_LENGTH) != 0)
	{
		printf("HMAC authentication failed!\n");
		return false;
	}

	printf("HMAC authentication success!\n");

	// decrypt {seqnum|command_str}_Ksess
	SymmetricCipher sc(EVP_aes_128_cbc(), session_key, command_iv);
	unsigned char *command_plaintext;
	unsigned int command_plainlen;
	sc.decrypt(command_ciphertext, command_ciphertext_len);
	sc.decrypt_end();
	command_plainlen = sc.flush_plaintext(&command_plaintext);

	// verify sequence number
	uint64_t received_seqno;
	memcpy((void*)&received_seqno, command_plaintext, sizeof(uint64_t));

	char *command_text = (char*)&command_plaintext[sizeof(uint64_t)];
	unsigned int command_text_len = command_plainlen - sizeof(uint64_t); // Must be checked

	if(received_seqno != sr_seq_num)
	{
		printf("Invalid sequence number! (%lu != %lu)\n", received_seqno, sr_seq_num);
		return false;
	}

	// increment server sequence number
	sr_seq_num++;

	// return receive command
	*received_command = new unsigned char[command_text_len];
	memcpy(received_command, command_text, command_text_len);
	*received_command_len = command_text_len;

	printf("Received command: %s\n", command_text);

	return true;
}

bool send_command(int sd, char data_response[], unsigned int data_response_len)
{
	unsigned char *data_resp_iv = new unsigned char[EVP_CIPHER_iv_length(EVP_aes_128_cbc())];
	generate_iv(data_resp_iv);
	send_data(sd, data_resp_iv, EVP_CIPHER_iv_length(EVP_aes_128_cbc()));

	SymmetricCipher sc(EVP_aes_128_cbc(), session_key, data_resp_iv);

	// encrypt cl_seq_num|data_response
	sc.encrypt((unsigned char*)&cl_seq_num, sizeof(cl_seq_num));
	sc.encrypt((unsigned char*)data_response, data_response_len);
	sc.encrypt_end();
	unsigned char *command_ciphertext;
	unsigned int command_cipherlen = sc.flush_ciphertext(&command_ciphertext);

	// send {seqnum|data_response}_Ksess
	send_data(sd, command_ciphertext, command_cipherlen);

	// make hmac from {seqnum|data_response}_Ksess
	unsigned char *hash_result;
	unsigned int hash_len;

	HMACMaker hc(session_key, 16);
	hc.hash(command_ciphertext, command_cipherlen);
	hash_len = hc.hash_end(&hash_result);

	// send HMAC_Ksess{ eqnum|data_response}_Ksess }
	send_data(sd, hash_result, hash_len);

	// increment server sequence number
	cl_seq_num++;

	return true;
}


int main(int argc, char** argv)
{
	ERR_load_crypto_strings();

	int err = 0;
	uint16_t server_port;
	ConnectionTCP conn;
	my_buff.buf = NULL;
	my_buff.size = 0;
	send_file_msg s_msg;

	unsigned char *encrypted_key;
	unsigned int encrypted_key_len;
	unsigned char *iv;
	unsigned int iv_len=0;

	if( argc < 2 ){
		printf("use: ./server port");
		return -1;
	}

	sscanf(argv[1],"%hd",&server_port);

	int sd = open_tcp_server(server_port);
	int cl_sd = accept_tcp_server(sd,&conn);

	// 1) Get Client Nonce
	if( !recv_hello_msg(cl_sd) )
		return -1;

	// 2) Send Server Nonce
	if ( !send_hello_msg(cl_sd) )
		return -1;

	// 3) Send Server verification infos
	// sign {client_nonce|server_nonce}_Kpub
	if ( !send_server_verification(cl_sd) )
		return -1;

	// 4/5) Check Client identity / Send auth response
	// receive {client_nonce|session key|username|password}_Kpub
	// send authok or authfailed
	if ( !check_client_identity(cl_sd) )
		return -1;

	// 6) Receive Command
	// receive {seqnum|command_str}_Ksess | HMAC{{seqnum|command_str}_Ksess}_Ksess
	unsigned char *received_command;
	unsigned int received_command_len;
	if(!receive_command(cl_sd, &received_command, &received_command_len))
		return -1;

	// 7) Send Response
	// send {seqnum|data_response}_Ksess | HMAC{{seqnum|data_response}_Ksess}_Ksess
	char data_response[] = "Nun c'ho nulla\nFine risposta";
	if(!send_command(cl_sd, data_response, strlen(data_response)+1))
		return -1;


	// ----------------------------------------------------------------

	//ricevo la chiave simmetrica
	encrypted_key_len = recv_data(cl_sd, &my_buff);
	encrypted_key = new unsigned char[encrypted_key_len];
	if( encrypted_key == NULL ) {
		printf("Cannot allocate encrypted_key\n");
		err = -1;
		return err;
	}
	memcpy(encrypted_key, my_buff.buf, encrypted_key_len);

	//ricevo l'iv
	iv_len = recv_data(cl_sd, &my_buff);
	iv = new unsigned char[iv_len];
	if( iv == NULL ) {
		printf("Cannot allocate iv \n");
		err = -1;
		return err;
	}

	memcpy(iv, my_buff.buf, iv_len);
	if( !recv_msg(cl_sd,&s_msg,SEND_FILE) )
	{
		printf("Errore ricezione messaggio SEND_FILE \n");
		return -1;
	}
	printf("Riceverò %d chunk di dimensione:%d \n",s_msg.chunk_number,s_msg.chunk_size);

	DecryptSession ds("keys/rsa_server_privkey.pem", encrypted_key, encrypted_key_len, iv);

	FILE *fp;
	open_file_w("ricevuto.txt", &fp);

	unsigned int total_plainlen = 0;
	// initialize receive buffer
	my_buffer chunk_cipher;
	chunk_cipher.buf = NULL;
	chunk_cipher.size = 0;
	for(unsigned int i=0; i < s_msg.chunk_number; i++)
	{
		// get chunk from tcp socket
		unsigned int chunk_cipherlen = recv_data(cl_sd, &chunk_cipher);
		printf("decrypting chunk(%d) of %d ciphertext bytes\n", i, chunk_cipherlen);

		// do decryption
		unsigned char* chunk_plaintext;
		ds.decrypt(chunk_cipher.buf, chunk_cipherlen);
		unsigned int chunk_plainlen = ds.flush_plaintext(&chunk_plaintext);
		total_plainlen += chunk_plainlen;

		// write to file
		fwrite(chunk_plaintext, 1, chunk_plainlen, fp);

		// if latest chunk, compute padding
		if(i == s_msg.chunk_number-1)
		{
			unsigned char *padding_plaintext;
			ds.decrypt_end();
			unsigned padding_plainlen = ds.flush_plaintext(&padding_plaintext);
			total_plainlen += padding_plainlen;
			printf("adding last padded block of %d bytes\n", padding_plainlen);

			// write latest block (without padding)
			fwrite(padding_plaintext, 1, padding_plainlen, fp);
		}
	}
}