#ifndef SAMPLE_TEXT_H
#define SAMPLE_TEXT_H

#include <Arduino.h>

// Page delimiter for splitting text into pages
const char SAMPLE_TEXT[] PROGMEM = R"(Chapter 1: The Beginning

In the realm of electronic paper displays, there existed a device known as the MicroReader. This remarkable gadget combined the comfort of reading traditional books with the convenience of modern technology. The e-ink screen displayed text with remarkable clarity, mimicking the appearance of ink on paper. No harsh backlight strained the eyes during long reading sessions. Readers could enjoy their favorite books for hours without fatigue.

The technology behind e-ink displays was fascinating. Tiny microcapsules containing black and white particles responded to electrical charges, creating images that remained visible without power. This bistable nature meant the display only consumed energy when changing pages, allowing for weeks of battery life. The contrast ratio rivaled printed paper, making text crisp and legible even in direct sunlight.

---PAGE---

Chapter 2: Discovery

Engineers had spent years perfecting the technology. The custom LUT configurations allowed for rapid page refreshes, making the reading experience smooth and natural. Partial refresh modes eliminated the distracting full-screen flashes that plagued earlier generations of e-readers. Text appeared instantly, as if by magic.

Research teams experimented with different waveform patterns to optimize refresh speed while maintaining image quality. They discovered that by carefully controlling voltage sequences, they could achieve sub-second page turns. The breakthrough came when they realized that partial updates only needed to modify changed pixels, dramatically reducing refresh time.

Temperature compensation proved crucial for consistent performance. The viscosity of the electronic ink particles varied with temperature, affecting their movement speed. By monitoring the ambient temperature and adjusting waveforms accordingly, engineers ensured reliable operation in diverse environments from cold winter mornings to hot summer afternoons.

---PAGE---

Chapter 3: Innovation

The device featured custom fonts rendered through Adafruit-GFX, providing crisp, professional typography. FreeSans fonts in multiple sizes offered excellent readability. Button navigation made page turning effortless. A simple press of the right button advanced to the next page, while the left button returned to previous content.

Text layout algorithms implemented sophisticated word wrapping and justification. Each line was carefully measured to ensure words fit properly within margins. Spacing between words was dynamically calculated to create even distribution across the line width, producing a polished, professional appearance reminiscent of printed books.

The user interface was designed with simplicity in mind. Six buttons provided complete control without overwhelming the user with options. Volume buttons adjusted refresh speed settings, allowing readers to prioritize either speed or quality based on their preferences. The confirm button switched between demonstration mode and reading mode, while the back button triggered full refreshes to clear any ghosting artifacts.

---PAGE---

Chapter 4: Implementation

Behind the scenes, the SSD1677 display controller managed complex waveforms and voltage sequences. The ESP32-C3 microcontroller orchestrated everything with precision timing. Frame buffers stored pixel data efficiently, with each bit representing a single black or white pixel on the 800x480 display.

The software architecture separated concerns into distinct layers. The EInkDisplay class handled low-level hardware communication, managing SPI transfers and display controller commands. The TextRenderer class provided a familiar graphics interface, extending Adafruit-GFX to render directly into the frame buffer. The DisplayController class implemented high-level application logic, managing page navigation and mode switching.

Memory management was critical on the resource-constrained microcontroller. The frame buffer alone required 48 kilobytes of RAM. Careful optimization ensured smooth operation without exhausting available memory. Text content was stored in flash memory using PROGMEM, freeing precious RAM for runtime operations. String parsing happened incrementally to avoid creating large temporary buffers.

---PAGE---

Chapter 5: The Future

As technology advanced, e-ink displays became increasingly sophisticated. Higher refresh rates, improved contrast, and larger screens opened new possibilities. The MicroReader represented just the beginning. Future iterations would bring color displays, touchscreens, and even faster refresh rates.

Color e-ink technology was rapidly maturing. Advanced Color ePaper displays could render thousands of colors while maintaining the same low power consumption and readability advantages. Imagine reading vibrant comic books or viewing colorful diagrams on an e-ink screen that remained perfectly legible outdoors.

Flexible displays promised new form factors. Imagine a reader that could be rolled up like a scroll for easy portability, then unfurled to reveal a large reading surface. Rugged, durable screens could withstand drops and impacts that would shatter traditional displays. The possibilities seemed endless as researchers continued pushing the boundaries of what electronic paper could achieve.

---PAGE---

Chapter 6: The Reading Experience

The true measure of any reading device lies in the experience it provides. The MicroReader excelled at creating an immersive environment where technology faded into the background, allowing the content to take center stage. Page turns felt natural and immediate. Text flowed smoothly across the screen with proper spacing and alignment.

Readers appreciated the ability to customize their experience. Font sizes could be adjusted to suit individual preferences and lighting conditions. Margins provided comfortable whitespace that framed the text without wasting precious screen real estate. Line spacing was carefully calibrated to enhance readability while maximizing the amount of text visible on each page.

The device encouraged focused reading free from the distractions of notifications and connectivity. Unlike tablets and phones with their constant interruptions, the MicroReader provided a sanctuary for deep concentration. Hours could pass unnoticed as readers lost themselves in stories, emerging only when the final page had been turned.

---PAGE---

Epilogue

And so the journey continued, with each page turn revealing new knowledge and new adventures in the world of electronic reading devices. The marriage of centuries-old reading traditions with cutting-edge technology had produced something special. A device that honored the past while embracing the future.

In libraries and coffee shops, on trains and beaches, MicroReader users discovered the joy of reading reimagined. The gentle tap of buttons replaced the rustle of paper pages, but the fundamental pleasure of engaging with great literature remained unchanged. Stories still transported readers to distant lands and different times. Knowledge still illuminated minds eager to learn.

The end of this tale is merely the beginning of countless others waiting to be read. Close this cover, press that button, and let the next adventure begin.)";

#endif
