all : flash

TARGET:=ntsctest

TARGET_MCU?=CH32V003
include ../ch32fun/ch32fun/ch32fun.mk

jollywrencher.h : jollyvert jollywrencher.png
	./jollyvert | tee $@

jollyvert : jollyvert.c
	gcc -o $@ $^ -lm

flash : cv_flash
clean : cv_clean

