# BIOS image loading

An external BIOS input must be a regular file containing exactly 16,384 bytes. Missing, short, oversized, directory and pipe inputs fail without changing the current firmware or machine state. The loader reads a complete candidate and checks for trailing data, stream errors and close failure before installation. File length validates the input format; it does not authenticate its contents.

Successful loading selects external firmware and keeps an existing BIOS allocation and its execution mapping valid. The frontend then resets the machine to execute that firmware. A caller using the core loading API controls when to reset or refill the instruction pipeline. Loading does not advance emulated clocks or reset the CPU itself.

When `-b` is supplied, the SDL2 frontend stops on a loading error before system reset, save loading, video, audio or guest execution. The headless runner also rejects invalid requested BIOS input. Omitting the option continues to select the built-in replacement firmware.

Four native groups verify built-in and external firmware preservation, valid replacements and instruction execution after reset, and nonregular input rejection. The Linux case checks an unopened named pipe. Two process groups verify the frontend/headless startup boundaries and preservation of the existing save file. Their valid images are generated from owned test instructions; they do not qualify an external firmware's boot behavior.
