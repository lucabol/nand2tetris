// This file is part of www.nand2tetris.org
// and the book "The Elements of Computing Systems"
// by Nisan and Schocken, MIT Press.
// File name: projects/04/Fill.asm

// Runs an infinite loop that listens to the keyboard input.
// When a key is pressed (any key), the program blackens the screen,
// i.e. writes "black" in every pixel;
// the screen should remain fully black as long as the key is pressed. 
// When no key is pressed, the program clears the screen, i.e. writes
// "white" in every pixel;
// the screen should remain fully clear as long as no key is pressed.

  @24576
  D=A
  @screenend
  M=D

  @color
  M=-1

(LOOP)
  @SCREEN
  D=A
  @screenptr
  M=D

  // If no key is pressed goto white
  @KBD
  D=M
  @WHITE
  D;JEQ

  // Otherwise goto black
  @BLACK
  0;JMP

(WHITE)
  @color
  M=0
  @SCREENPRINT
  0;JMP

(BLACK)
  @color
  M=-1

(SCREENPRINT)
  // Get color (this should be outside the loop)
  @color
  D=M

  // Dereference pointer to current screen location
  @screenptr
  A=M
  M=D

  // Increase pointer
  @screenptr
  M=M+1
  
  // If not end of screen continue printing
  @screenptr
  D=M
  @screenend
  D=D-M

  @SCREENPRINT
  D;JLT

  // Else goto start
  @LOOP
  0;JMP




