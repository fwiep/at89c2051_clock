# AT89C2051-based, 6-digit clock firmware

![Image of assembled kit, source Alibaba](docs/Hc0b8935c95d342c58605cff0d9320548S.jpg)

## Introduction

A few months ago, someone asked my help in fixing this 6-digit digital clock kit
they had assembled. After soldering all the components, inserting the IC and
connecting the power supply, nothing happened. They found
[this video on YouTube](https://www.youtube.com/watch?v=OIdR2x1GxLo), explaining
that, instead of executable binary instructions, the microcontroller probably
contained part of the clock program's source code. Or even no programming at all!

So, the video's creator [Ruthsarian](https://github.com/ruthsarian) went on and
wrote their own firmware in C and published the project
[on Github](https://github.com/ruthsarian/at89c2051_clock). My project is
based on this work.

> [!NOTE]
> This project is *work out of progress*, i.e. no longer pursued by me.  
> As soon as I found a complete and well documented sourcecode in
> [this article by Ceptimus][434], I stopped my own development.  
> Thank you, Ceptimus!

## Customizations

The DIY soldering kit is available from, among others, [AliExpress][522] and
[Alibaba][523]. The original project used the same microcontroller, but had four
digits &ndash; this kit had six.

Being a programmer, but having no practical experience in assembler or C, I
managed to increase display count from 4 to 6 by studying both kit's schematics.
They differ in which of the chip's outputs select the display segments, and in
which output controls the (blinking) colons between hours and minutes, minutes
and seconds respectively. Finally, the button inputs differed, too.

![4-digit clock kit schematic, source Ruthsarian](docs/schematic.jpg)
*Original 4-digit clock schematic*

![6-digit clock kit schematic, source AliExpress](docs/2042_11.png)
*6-digit clock schematic*

As advised by Ruthsarian, I compiled the source using [SDCC version 3.5.0][457].
Any newer version would increas the compiled size to more than 2048 bytes &ndash;
the chip's maximum ROM capacity.

## Features

- Display HH:MM:SS
- Set time hour, time minute
- Set alarm hour, alarn minute
- Enable/disable alarm
- Blinking colons (1/2 second on, 1/2 second off)

## Edits to make it fit into 2k

- drop 12/24 hour support
- drop timer function

## Operation

There are three buttons, but only the two rightmost are used in this project.

## Programming the AT89C2051

I have been using an [SP200S-V2.0 programmer][144] to flash the compiled `.hex`-file
into the AT89C2051 microcontroller. Finding the right software to do that was a
challenge, but I eventually ended up using a virtual machine running Windows 10.
In there, I installed the flash tool [WL-Pro V220][633]. Additional drivers for
the serial port were not necessary; Windows already recognized the programmer's
`CH340` chip out of the box.

## References

- [AT89C2051 datasheet][844]

[144]: https://aliexpress.com/item/1005005921400025.html
[434]: https://ceptimus.co.uk/index.php/2024/01/22/3-button-at89c2051-clock-kits/
[457]: https://sourceforge.net/projects/sdcc/files/sdcc/3.5.0/
[522]: https://aliexpress.com/item/1005001671051111.html
[523]: https://www.alibaba.com/suppliersubdomainalibabacom/product-detail/I-1600154086618.html
[633]: https://w.electrodragon.com/w/USB-TTL_Programmer
[844]: https://ww1.microchip.com/downloads/en/DeviceDoc/doc0368.pdf
