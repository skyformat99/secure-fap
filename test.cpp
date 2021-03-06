#include "commonlib/commonlib.h"
#include <iostream>

using namespace std;
#undef CHUNK_SIZE 
#define CHUNK_SIZE 64

unsigned int divide_upper(unsigned int dividend, unsigned int divisor)
{
    return 1 + ((dividend - 1) / divisor);
}

int main(int argc, char **argv)
{
	DynamicArray arr;

	char msg1[] = "abc1";
	unsigned int size1 = sizeof(msg1)-1;
	char msg2[] = "dfghijkl2";
	unsigned int size2 = sizeof(msg2)-1;
	char msg3[] = "mnopqrstuvwxyz4";
	unsigned int size3 = sizeof(msg3)-1;
	char msg4[] = "56789";
	unsigned int size4 = sizeof(msg4);

	printf("%s\n", arr.getArray());
	arr.appendBytes((unsigned char*)msg1, size1);
	printf("%s\n", arr.getArray());
	arr.appendBytes((unsigned char*)msg2, size2);
	printf("%s\n", arr.getArray());
	arr.appendBytes((unsigned char*)msg3, size3);
	printf("%s\n", arr.getArray());
	arr.appendBytes((unsigned char*)msg4, size4);
	printf("%s\n", arr.getArray());
return 0;
	unsigned char key[16];
	unsigned char plaintext_0[] = "Lorem ipsum dolor sit amet, consectetur adipisci elit, sed eiusmod tempor incidunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur. Quis aute iure reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint obcaecat cupiditat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";
	unsigned char *ciphertext_0;
	unsigned char *plaintext_1;

	printf("plaintext_0: \n");
	print_hex(plaintext_0, sizeof(plaintext_0));
	printf("------------------\n");

	SymmetricCipher sc(EVP_aes_128_cbc(),key,NULL);
	unsigned int chunk_cipherlen = sc.encrypt(plaintext_0, CHUNK_SIZE, &ciphertext_0);

	printf("ciphertext_0: \n");
	print_hex(ciphertext_0, chunk_cipherlen);
	printf("------------------\n");

	unsigned int chunk_plainlen = sc.decrypt(ciphertext_0, chunk_cipherlen, &plaintext_1);

	printf("plaintext_1: \n");
	print_hex(plaintext_1, chunk_plainlen);
	printf("------------------\n");

	printf("confronto key:%d \n",memcmp(key,sc.get_key(),16));
	return 0;
}