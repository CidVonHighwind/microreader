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

Chapter 7: Engineering Excellence

The development process demanded meticulous attention to detail. Every millisecond of refresh time mattered when optimizing the user experience. Engineers profiled code paths, identifying bottlenecks and opportunities for improvement. SPI clock speeds were pushed to their limits, carefully balanced against signal integrity concerns.

Hardware design choices significantly impacted performance. The selection of GPIO pins for the SPI interface considered electrical characteristics and routing efficiency. Proper decoupling capacitors ensured stable power delivery during the current spikes that accompanied display refreshes. Every component was chosen with purpose and precision.

Testing revealed unexpected challenges. Temperature extremes affected display behavior in subtle ways. Electromagnetic interference from nearby electronics occasionally caused communication errors. Mechanical stress from repeated button presses required robust debouncing algorithms. Each issue was methodically investigated, understood, and resolved through iterative refinement.

---PAGE---

Chapter 8: The Art of Typography

Beautiful typography elevates the reading experience from functional to delightful. The choice of fonts reflected careful consideration of readability and aesthetics. FreeSans offered clean, modern letterforms that rendered well at small sizes. Character spacing and kerning pairs were optimized for the display's pixel density.

Line breaks followed sophisticated algorithms that balanced aesthetic concerns with practical constraints. Hyphenation was avoided when possible, preserving word integrity. Widows and orphans, those lonely single lines at the start or end of pages, were eliminated through intelligent reflow. The result appeared professionally typeset, as if prepared by skilled printers rather than generated algorithmically.

Paragraph indentation and spacing created visual rhythm that guided the eye naturally down the page. First-line indents marked new paragraphs without requiring wasteful blank lines. Consistent leading between lines of text maintained comfortable reading flow. These subtle details accumulated into a polished, professional presentation that honored the written word.

---PAGE---

Chapter 9: Power Management

Energy efficiency distinguished e-ink technology from its LCD and OLED counterparts. The MicroReader could operate for weeks on a single charge, thanks to the bistable nature of the display. Pixels maintained their state without consuming power, drawing current only during page refreshes.

The ESP32-C3's deep sleep modes reduced idle power consumption to mere microamperes. When reading paused, the system entered hibernation, preserving battery life while remaining instantly ready to resume. Wake-up times were measured in milliseconds, providing seamless transitions from sleep to active states.

Battery management circuitry protected against overcharging and excessive discharge. Lithium polymer cells delivered hundreds of charge cycles with minimal capacity degradation. Users could read for extended periods without anxiety about battery life, focusing entirely on their books rather than power meters and charging schedules.

---PAGE---

Chapter 10: Software Architecture

Clean code architecture made the MicroReader maintainable and extensible. Object-oriented design principles separated hardware abstraction from business logic. Interfaces defined clear contracts between components, enabling independent development and testing of each subsystem.

The display driver encapsulated all hardware-specific details, presenting a simple API to higher-level code. Buffer management, SPI communication, and timing sequences remained hidden behind well-defined methods. This abstraction allowed the same application code to work with different display controllers by simply swapping out the driver implementation.

Text rendering leveraged the Adafruit GFX library's extensive capabilities while extending it with custom functionality. Font rendering, line wrapping, and justification algorithms integrated seamlessly with the existing graphics framework. The resulting system combined third-party code reliability with project-specific optimizations.

---PAGE---

Chapter 11: User Testing

Real users provided invaluable feedback that shaped the final product. Beta testers reported reading habits, preferences, and pain points that designers had overlooked. Some preferred faster page turns even at the cost of slight ghosting. Others prioritized pristine image quality above all else. The configurable refresh modes accommodated both preferences.

Button placement evolved through multiple iterations. Early prototypes positioned controls in locations that caused accidental presses. User testing revealed more ergonomic arrangements that felt natural during extended reading sessions. The final layout allowed one-handed operation, with commonly-used controls falling under the thumb's natural resting position.

Accessibility considerations ensured the device served diverse users. Font size options accommodated varying vision capabilities. High contrast rendering remained legible for users with reduced visual acuity. Simple, predictable navigation allowed users of all technical skill levels to operate the device confidently without consulting manuals or help documentation.

---PAGE---

Chapter 12: Manufacturing

Production at scale introduced challenges distinct from prototype development. Component sourcing required identifying reliable suppliers who could deliver consistent quality. Manufacturing yields needed optimization to control costs while maintaining high standards. Automated testing verified each unit's functionality before packaging and shipment.

Quality control processes caught defects early, preventing flawed units from reaching customers. Electrical testing validated power consumption and signal integrity. Display functionality was verified through automated test patterns. Button response was checked mechanically to ensure proper activation force and tactile feedback.

Documentation supported manufacturing operations and field service. Assembly instructions guided technicians through each step with clear photographs and diagrams. Troubleshooting guides helped diagnose common issues. Repair manuals enabled service centers to efficiently restore malfunctioning units to working condition.

---PAGE---

Chapter 13: The Community

Users formed communities online, sharing favorite books and tips for optimal device usage. Forums buzzed with discussions about font preferences and page layout configurations. Enthusiasts posted photographs of their devices in exotic reading locations, from mountain peaks to tropical beaches.

Developers contributed improvements to the open-source firmware. Bug fixes and feature enhancements emerged from collaborative development efforts. The community tested pre-release versions, providing feedback that shaped each new update. This participatory approach created software that truly served user needs rather than following designer assumptions.

Content creators adapted their works specifically for e-ink displays. Text formatting considered the display's capabilities and constraints. Graphics were optimized for monochrome rendering. Publishers recognized e-ink readers as a distinct platform deserving custom consideration rather than simply reformatting content designed for other mediums.

---PAGE---

Chapter 14: Environmental Impact

Electronic readers reduced paper consumption, saving trees and reducing waste. A single device replaced hundreds of physical books, dramatically shrinking the carbon footprint of an avid reader's library. Manufacturing emissions were amortized across years of use rather than concentrated in single-use products.

E-ink displays consumed minimal power compared to backlit screens, reducing energy demand. Lower power consumption meant smaller batteries, which reduced toxic materials in manufacturing. The devices' long operational life prevented premature obsolescence that plagued other consumer electronics with shorter useful lifespans.

Recycling programs ensured end-of-life devices were responsibly dismantled and materials recovered. Component manufacturers accepted returned products, extracting valuable metals and properly disposing of hazardous substances. This circular approach minimized environmental impact across the entire product lifecycle.

---PAGE---

Chapter 15: Looking Forward

The future promised exciting developments in electronic reading technology. Advanced displays with higher resolutions would render text with even greater clarity. Color e-ink displays would bring magazines and textbooks to life with vibrant illustrations. Faster refresh rates would enable new applications beyond static text, perhaps even video playback.

Artificial intelligence might personalize the reading experience, suggesting content based on preferences and reading patterns. Smart summarization could help readers navigate lengthy documents efficiently. Language translation services could provide instant definitions and cultural context, enriching comprehension of foreign works.

Connectivity features could enable sharing highlights and notes with friends. Book clubs might meet virtually within the reading interface itself. Authors could receive feedback directly from readers, creating new channels for literary discourse. The boundary between reading device and social platform might blur, creating new forms of literary engagement.

---PAGE---

Epilogue

And so the journey continued, with each page turn revealing new knowledge and new adventures in the world of electronic reading devices. The marriage of centuries-old reading traditions with cutting-edge technology had produced something special. A device that honored the past while embracing the future.

In libraries and coffee shops, on trains and beaches, MicroReader users discovered the joy of reading reimagined. The gentle tap of buttons replaced the rustle of paper pages, but the fundamental pleasure of engaging with great literature remained unchanged. Stories still transported readers to distant lands and different times. Knowledge still illuminated minds eager to learn.

The end of this tale is merely the beginning of countless others waiting to be read. Close this cover, press that button, and let the next adventure begin.)";

#endif
