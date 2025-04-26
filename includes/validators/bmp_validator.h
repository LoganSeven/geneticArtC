/**
 * @file bmp_validator.h
 * @brief BMP file validation utilities.
 *
 * Declares functions to validate the basic structure of a BMP file
 * before loading it as a surface.
 */

 #ifndef BMP_VALIDATOR_H
 #define BMP_VALIDATOR_H
 
 #include <stdbool.h>
 
 /**
  * @brief Checks if the given file is a valid BMP.
  * @param filename Path to the BMP file.
  * @return true if the file passes basic format checks, false otherwise.
  */
 bool bmp_is_valid(const char *filename);
 
 #endif /* BMP_VALIDATOR_H */
