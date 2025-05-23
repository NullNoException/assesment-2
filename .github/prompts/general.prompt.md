2 Introduction
We are developing a new service aimed at revolutionising document editing, with the ambitious
goal of outperforming competitors such as Word, Notion and Google Docs. To achieve this,
we must implement a server-client architecture where a central server hosts the document and
coordinates all client interactions. Multiple authorised clients will be able to send commands
to the server to edit the document concurrently.
The document format will be a modified version of Markdown. Content will be stored as plain
text using only ASCII characters, simplifying compatibility and transmission.
The server will always maintain the most up-to-date and accurate version of the document. Note
that if your data structure is a simple character array, inserting characters into the middle of the
document would require shifting all subsequent characters, resulting in inefficiency. Therefore,
you are advised to use a more suitable data structure, such as a linked list, to allow for efficient
editing operations.
The structures and basic operations can be found in the code scaffold You must adhere to the
function signatures for the marking script to correctly assess your code.
3 Task
Implement a simulated collaborative document editing system in C, consisting of two pro-
grams:

1. server — the authoritative document host
   • Listens for client connections over named pipes.
   • Maintains the document in an efficient in-memory structure.
   • Applies editing commands (INSERT, DEL, BOLD...) atomically and in the order
   received.
   • Broadcasts updates to all connected clients.
   • Handles client registration, graceful disconnects, and error reporting.
2. client — a console-based editor
   • Connects to the server’s named pipe endpoint.
   • Sends user commands to read or modify the document.
   • Receives update notifications and applies them to its local view.
   Deliverables & Constraints
   • Source files: server.c, client.c, plus any helpers.
   • Must compile on Edstem Linux environment using gcc (-Wall -Wextra).
   -std=c11 with no warnings
   Systems Programming Page 5 of 19
   COMP2017 9017
   4 Set-up
   Client–Server Connection Protocol
3. Start the server by running it with the following command. <TIME time interval in milliseconds at which the versions of the document updates.
   ./server <TIME INTERVAL>
   INTERVAL> is the
4. The server starts and immediately prints its Process ID (PID) to stdout:
   Server PID: <pid>
5. A client joins by invoking:
   ./client <server_pid> <username>
6. Upon starting, the client immediately sends SIGRTMIN to the server’s PID and then
   blocks, waiting for SIGRTMIN+1. The staff client will timeout after 1 second if no
   signal is received12. Student will not need to handle these conditions.
7. On receiving SIGRTMIN, the server:
   5.1. Spawns a new POSIX thread to handle that client.
   5.2. Within that thread, creates two named FIFOs to establish bi-directional communi-
   cation:
   • FIFO*C2S*<client*pid> (Client-to-Server)
   • FIFO_S2C*<client_pid> (Server-to-Client)
   • If either already exist, clean them up first and then create them.
   5.3. Sends SIGRTMIN+1 back to the client.
8. The client, upon receiving SIGRTMIN+1, opens:
   • FIFO*C2S*<client*pid> for writing, and
   • FIFO_S2C*<client*pid> for reading,
   and writes its username into FIFO_C2S*<client_pid>.
   1The timeout should not block the server thread, or the server process
   2Timeout is approximate
   Systems Programming Page 6 of 19
   COMP2017 9017
   Authorisation and Role Communication
9. After receiving the client’s username (as a newline-terminated ASCII string), the server
   must:
   (1) Check the provided username against the roles.txt file to determine if the user
   is authorised and what permission level they possess.
   (2) If the username is found, the server will send the following through FIFO*S2C*
   <client*pid>:
   • The server must send the user’s role (permission) as a single, lowercase ASCII
   string, terminated by exactly one newline character (\n). The role string must
   be one of the following exact values: write, or read.
   • After sending the role, the server must send the full document contents, fol-
   lowed immediately by entering the edit-command loop.
   (3) If the username is not found:
   • The server must send a rejection message: Reject UNAUTHORISED.
   • Wait for 1 second on this thread alone, not blocking the whole process3
   .
   • Then close both FIFOs, unlink them (i.e., delete them from the filesystem), and
   terminate the server thread handling that client.
   roles.txt Format
   The roles.txt file must reside in the same directory as the running server executable. Each
   line of the file contains a username followed by a permission level, separated by one or more
   spaces or tab characters. The valid permission levels are:
   • write — permission to edit the document.
   • read — permission to view the document but not modify it.
   Example format:
   1 bob write
   2 eve read
   3 ryan write
   Leading and trailing whitespace around usernames and roles must be ignored. Comparisons
   must be case-sensitive.
   Document Transmission Protocol
   After sending the role string, the server must transmit the full document contents to the client
   over the named pipe FIFO_S2C*<client_pid>, following these rules:
   3Wait time is approximate
   Systems Programming Page 7 of 19
   COMP2017 9017
   • The document must be serialised as plain ASCII text, preserving all internal newlines
   (‘\n‘) exactly as they exist in the server’s current document state.
   • The document’s current version will be sent as a newline terminated number, this is
   guaranteed to be smaller than uint64.
   • The document length in bytes will be first send as an integer followed by a newline
   delimiter. The size of the document in bytes is guaranteed to be smaller than uint64.
   • The entire document must be sent as a length prefixed byte stream.
   • The document content must be immediately readable by the client.
   • Transmission must continue after the entire document content is written to the pipe.
   Overall example payload:
   write\n // role
   0\n // version number
   34\n // document length
   This is the document.\n
   Hello world.
   Signal Handling Hints (Optional)
   Whilst the following functions are not allocated marks, correct behaviour is marked.
   • Use sigaction() to register a handler for SIGRTMIN and use sigqueue() for
   sending signals if you want to include additional data (e.g., client PID).
   • Use sigemptyset(), sigaddset(), and sigprocmask() to block SIGRTMIN
   and SIGRTMIN+1 before creating the thread. This prevents signal loss during handler
   setup.
   • Use sigwait() in the client to synchronously wait for SIGRTMIN+1—this avoids
   race conditions and allows clean blocking until the server responds.
   • Realtime signals (like SIGRTMIN) are queued by the kernel and not merged, making
   them safer than standard signals (like SIGUSR1) in high-concurrency environments.
   5 Structure
   Both the server and the client need to store a copy of the document. The server’s copy will
   always be the most up-to-date and the "true" state of the document, whilst the client’s copy will
   be updated as changes are broadcasted from the server.
   This is necessary to prevent constantly transmitting the entire document over the network,
   which slows down the app.
   Systems Programming Page 8 of 19
   COMP2017 9017
   The following table contains the formatting that is supported by the document.
   Element Markdown Syntax
   Heading 1 # H1
   Heading 2 ## H2
   Heading 3 ### H3
   Bold **bold text**
   Italic _italicised text_
   Blockquote > blockquote
   Ordered List 1. First item
10. Second item
11. Third item
    Unordered List - First item

- Second item
- Third item
  Code`code`
  Horizontal Rule---
  Link [title](https://www.example.com)
  Table 1: Markdown Syntax Reference
  Formatting Notes: Note although the following rules must be adhere to when adding format-
  ting, markdown is just plain text and the added formatting will not be treated differently to plain
  text.
  • Space Requirement for Certain Tags: The following Markdown tags must be followed
  by a space character (‘ ‘) immediately after the tag to be correctly parsed:
  – Heading markers: #, ##, ###
  – Blockquote marker: >
  – List item markers: 1., 2., 3.,-
  If a space is omitted after these tags, the element will not be recognised as correct for-
  matting and any of our (hypothetical) markdown renderer would not be able to correctly
  render it.
  • Block-level Elements: The following elements are classified as block-level:
  – Headings (Heading 1, Heading 2, Heading 3)
  – Blockquotes
  – Ordered Lists
  – Unordered Lists
  Systems Programming Page 9 of 19
  COMP2017 9017
  – Horizontal Rules
  • Newline Requirements for Block-level Elements: When inserting a block-level ele-
  ment, the editor must:
  – Ensure there is a newline character (‘\n‘) immediately preceding the inserted block
  element, unless it is inserted at the start of the document.
  – If the insertion point is not already at the beginning of a line (i.e., the previous
  character is not a newline), automatically insert a newline before the block element.
  – The horizontal rule must have newline after as well. If one does not exist, automat-
  ically insert one.
  • Inline Elements: Inline elements — specifically Bold, Italic, Code, and Links — do not
  require preceding or trailing newlines. They may appear within an existing line of text
  without requiring structural separation.
  6 Client-Server Synchronisation
  Since both the server and clients maintain a copy of the document state, it is critical to ensure
  consistent synchronisation of all changes across the system.
  All modifications to the document must originate as requests from clients, sent to the server in
  the form of commands.
  At intervals, the server will issue a new version of the document. It will broadcast all changes
  made since the previous version to all connected clients, ensuring synchronisation.
  To maintain consistency, the server will only accept commands that target the current version
  of the document. Any command referencing an outdated version will be rejected.
  It is guaranteed that no two commands will be received at the EXACT same time and operate
  at overlapping cursor positions. Targeting overlapping cursor positions of the same version of
  the document is allowed.
  7 Functionality
  7.1 Part 1: Single Client
  Commands are issued by the client and sent to the server for processing. Each command affects
  the document and must specify a cursor position, which is defined as the number of characters
  from the beginning of the document to the intended location.
  To simulate user interaction with a GUI, users may type commands directly into the standard
  input of the client or server.
  All positions are interpreted on the version they reference, and if valid, changes are reflected in
  the next broadcasted version. Once edits are made, future edits will need to have their cursor
  positions adjusted if necessary.
  Systems Programming Page 10 of 19
  COMP2017 9017
  Command Format Constraints
  All commands transmitted between the client and server must satisfy the following properties:
  • The command must be composed exclusively of printable ASCII characters (byte values
  32–126), with the exception of the final newline character.
  • Each command must be terminated by exactly one newline character ‘\n‘.
  • The total size of the command, including the terminating newline character, must not
  exceed 256 bytes.
  • Commands that do not satisfy these constraints (e.g., missing newline, exceeding 256
  bytes, or containing non-ASCII characters) are considered invalid and must be rejected
  by the receiving side.
  • All command arguments are separated by a single space character.
  • For commands with a cursor range, end position should not equal to or precede the start
  position.
  • The following commands are all also valid terminal commands.
  • INSERT and DEL commands that causes formatting errors or broken formatting can
  happen but do not need to be handled in any way, you can just accept them.
  Example:
  0 1 2 3 4 5 6 7 8
  D o c u m e n t
  Position 0 is before the first character D.
  Position 8 is after the last character t.
  Editing Commands
  • INSERT <pos> <content>; Inserts textual content at the specified cursor position
  and document version. Newlines are NOT permitted in this command.
  • DEL <pos> <no_char>; Deletes <no_char> characters starting from the specified
  cursor position. If the deletion flows beyond the end of the document, truncate at the end
  of the document.
  Systems Programming Page 11 of 19
  COMP2017 9017
  Formatting Commands
  Make sure you adhere to the formatting notes 5!
  • NEWLINE <pos>; Inserts newline formatting at the position.
  • HEADING fied position.
  <level> <pos>; Inserts a heading of the given level (1 to 3) at the speci-
  • BOLD <pos_start> <pos_end>; Applies bold formatting from the start to end
  cursor position.
  • ITALIC <pos_start> <pos_end>; Applies italic formatting from the start to end
  cursor position.
  • BLOCKQUOTE <pos>; Adds blockquote formatting at the specified position.
  • ORDERED_LIST <pos>; Adds ordered list formatting at the position. Any surround-
  ing list items will be renumbered appropriately (e.g., 1, 2, 3...). For example, if an ordered
  list is added to the end, it’ll find the previous number and set the new item to previous
  number +1. If an ordered list is added in between existing ordered list blocks, it’ll be the
  previous item + 1 and all list items will also be + 1. You must handle newlines which can
  break ordered lists into two. Delete that breaks formatting does NOT need to be handled.
  • UNORDERED_LIST <pos>; Applies unordered list formatting at the specified loca-
  tion.
  • CODE <pos_start> <pos_end>; Applies inline code formatting to the given range.
  • HORIZONTAL_RULE tion.
  <pos>; Inserts a horizontal rule and newline at the cursor posi-
  • LINK <pos_start> <pos_end> <link>; Wraps the text between positions in
  [] and appends the link in () to create a Markdown-style hyperlink.
  7.1.1 Ordered List
  Consider a list:

1. tomato
2. cheese burger
3. asparagus
   If I send a command ORDERED_LIST targeting the cursor position between cheese and burger,
   the list becomes:
4. tomato
5. cheese
6. burger
7. asparagus
   Systems Programming Page 12 of 19
   COMP2017 9017
   Also if I send a NEWLINE command to the same cursor position to the original list, it becomes:
8. tomato
9. cheese
   burger
10. asparagus
    If I send a NEWLINE command to the cursor position between 2 and . to the first list, it
    becomes:
11. tomato
    2
    . cheese burger
12. asparagus
    List formatting is guaranteed to not be sent if:
    • The target cursor position is at or next to a list formatting 2. or-
    .
    • The target is a list item formatted as a different type. For example sending ORDERED_LIST
    between love and gaming- I love gaming.
    Nested lists are not allowed and guaranteed to not happen. DELETE that causes formatting
    problems do NOT need to be considered.
    Also note, ordered list numbering is limited 1 to 9.
    Metadata and Other Commands
    • DISCONNECT; Disconnects the client from the server.
    Client Debugging Commands
    • DOC?; Prints the entire document to the client’s terminal.
    • PERM?; Requests and displays the client’s current role/permissions from the server.
    • LOG?; Outputs a full log of all executed commands in order.
    Server Responses
    • SUCCESS; Indicates that the client’s command was successfully executed.
    • Reject <reason>; Indicates the command was rejected, with a message detailing
    the reason. See Section 7.4.
    These responses are NOT sent immediately, but only sent as part of the broadcast message.
    Systems Programming Page 13 of 19
    COMP2017 9017
    Server Debugging Commands
    • DOC?; Prints the current document state to the server terminal.
    • LOG?; Outputs a full log of all executed commands in order.
    Output Formatting
    If the command required output onto the terminal, place a new line after the input command
    and print the output, for example:
    DOC?
    This is the document
    This is an example of the LOG? command and its output:
    LOG?
    VERSION 1
    EDIT <user> <command> SUCCESS // first command
    EDIT <user> <command> Reject <reason> // second command
    EDIT <user> <command> SUCCESS // third command
    VERSION 2
    EDIT <user> <command> SUCCESS // first command
    EDIT <user> <command> SUCCESS // second command
    ...
    EDIT <user> <command> SUCCESS // nth command
    ...
    VERSION <N>
    ...
    Not that the message for each version is the same as the message broadcasted to the clients at
    that version’s update.
    7.2 Part 2: Multiple Client
    If your collaborative document contains multiple users, multiple edits may occur simultane-
    ously. To handle this, the server spawns a separate thread for each connected client. Commands
    are queued within each thread. At the TIME INTERVAL update interval, the main server will
    collect all the commands from the thread queues based on timestamp order and process the
    changes in order. The server adds the timestamp when a command arrives.
    Handling Deleted Text and Cursor Position Adjustments All the edits target the broad-
    casted document, i.e. what the client was looking at when sending the command. Generally,
    no matter the execution order of the commands queued for a version, the final document will
    look the same. However for commands with overlapping cursors, this is not true and executing
    the commands in the order of timestamp arrival will be critical.
    Systems Programming Page 14 of 19
    COMP2017 9017
    If a section of text is deleted and a future command (from any client) operating on the same
    version is to run at a cursor position within that deleted region, apply the following rules:
    • For commands with a single cursor position, replace the cursor position with the loca-
    tion where the deletion began.
    Example: If positions 3 to 9 were deleted in an earlier command on the same version, an
    insertion at position 5 will now occur at position 3.
    • For commands requiring starting and ending cursor positions:
    – If both positions fall within the deleted region, it’s a DELETED_POSITION error.
    – If only one position is within the deleted region, the cursor position within the
    deleted region will be adjusted to the closer of the deleted edges to the valid cursor
    position.
    If a section of text is formatted or inserted and a cursor region which overlaps this change is
    deleted by a future command targeting the same version, apply the following rules:
    • For insertions, the deleted will deleted the original intended deletion and the new inser-
    tion is untouched.
    Consider a sentence: "Hello world"
    – Insert at cursor position 5: " beautiful"
    – Then delete starting at position 0, deleting 100 characters
    The final result is:
    " beautiful"
    • For formatting commands requiring a single cursor position, that formatting change will
    be applied and deleted along with the original intended deletion.
    • For formatting commands requiring starting and ending cursor positions:
    – If both positions fall within the deleted region, that formatting change will applied
    and deleted along with the original intended deletion.
    – If only one position is within the deleted region, that position is deleted and the
    formatting is broken.
    Versioning System To address the issue of commands arriving in different orders, a version-
    ing system is used. This system keeps track of the user’s originally intended edit position and
    adjusts cursor positions accordingly.
    • A new document version is issued every TIME • Version numbers start from 0 and increment by 1.
    INTERVAL.
    Systems Programming Page 15 of 19
    COMP2017 9017
    • Even if no changes were made, it’ll broadcast just the version number.
    • All of the following messages are newline terminated.
    • The payload must be closed with an END message.
    • The following message is broadcast to all users when a new document version is issued:
    VERSION <version_number>
    EDIT <user> <command> SUCCESS // first command
    EDIT <user> <command> Reject <reason> // second command
    ...
    END
    EDIT <user> <command> SUCCESS // nth command
    This ensures every client can sync their local copy of the document to the most up-to-date
    version.
    Edit Validation The server will only accept edits on the current document version. Any
    attempt to edit an older version will be rejected.
    7.3 COMP9017
    Implement the server such that both edits on version N and N-1 are accepted.
    You must document and justify your program’s behaviour in a file named README.md. In
    this, you must justify, AND demonstrate with testcases with sufficient complexity (incl. edge
    cases), why your implementation is well-defined and makes intuitive sense.
    COMP9017 students have their marks scaled by 0.9. This component counts for 10%.
    7.4 Rejections
    The server may reject a client’s command for several reasons. In such cases, it will respond
    with a rejection message in the format:
    Reject <reason>
    Below is a list of possible rejection reasons:
    UNAUTHORISED <command> <required permission> <current permission>
    The client does not have sufficient permissions (i.e., write) to execute the specified
    command.
    INVALID_POSITION One or more provided cursor positions are outside the bounds of the
    current document. For cursor ranges, the end position is equal to or lower than the start
    position.
    Systems Programming Page 16 of 19
    COMP2017 9017
    DELETED_POSITION The specified cursor position points to text that has already been deleted.
    OUTDATED_VERSION The version targeted by the command is no longer accepted by the
    server. Commands must reference either the current (or previous for COMP9017) docu-
    ment version.
    8 Tear Down
    If there no clients connected to the server, you can send QUIT via the terminal to the server to
    shut it down.
    In this case it’ll gracefully close the pipes, clean up all the memory, and free up all related
    resources. It’ll also save the document in a doc.md in the same directory. If such a file
    already exists, overwrite it.
    If any clients are still connected, it’ll print
    QUIT rejected, <number> clients still connected.
    . Example:
    QUIT
    QUIT rejected, 3 clients still connected.
    9 Marking
    Automated marking will be conducted through the following two categories:
    9.1 Markdown Unit Tests
    • A suite of tests will be run to verify the correctness of all Markdown functionality speci-
    fied in Section 7.
    • These tests include insertion, deletion, formatting commands (e.g., headings, bold, italic,
    blockquotes, lists), and version handling.
    • The tests will check for correct document state updates and proper command responses.
    • Submissions will also be checked for memory safety to ensure there are no memory leaks.
    9.2 Staff Client-Server Interoperability Tests
    • A reference staff client and staff server have been implemented to verify
    protocol compliance.
    • Your server must correctly interoperate with the staff client.
    Systems Programming Page 17 of 19
    COMP2017 9017
    • Your client must correctly interoperate with the staff server.
    • Interoperability will be verified using input-output based automated tests.
    • Strict adherence to the communication protocol (signals, FIFO creation, document trans-
    mission format, command structure) is essential for passing these tests.
    10 Short Answer Questions
13. How do you ensure that you do not have to constantly copy and shift large chunks of memory
    in your code?
14. If multiple edits are made to the same version of the document, how do you make sure the
    cursor positions where any edits are made are what the client originally intended.
15. How did you (or would you) test multiple clients interacting with the server at the same
    time? Does your test case(s) target race conditions or concurrency-related edge cases?
16. How did you efficiently ensure that all the formatting of ordered lists were correct? If not,
    how did you repair them?
17. How can select or epoll change our implementation? Are threads still necessary?
18. Is our program more efficient or runs better than a single process single process model?
    Does non-blocking help? If so in what ways?
    11 Restrictions
    To successfully complete this assignment you must follow the restrictions below. Submissions
    breaking these restrictions will receive a deduction of up to 1 mark per breach.
    • The implementation of the server must make use of one POSIX thread per client.
    • The code must entirely be written in the C programming language.
    • Must use dynamic memory for document.
    • Free all dynamic memory that is used.
    • POSIX library is needed for POSIX threads, however other libraries such as regex.h
    is NOT allowed.
    • NOT use any external libraries other than those in libc.
    • NOT use VLAs.
    • NOT use shared memory.
    • NOT use regex
    • NOT have unclean repositories. This means no object, executable, or temporary files for
    any commit in the latest repository. A clean final commit is NOT good enough. You
    SHOULD use .gitignore to avoid committing garbage files.
    • Only include header files that contain declarations of functions and extern variables. Do
    not define functions within header files.
    • Any and all comments, including git commit messages, must be written only in the En-
    glish language.
    Systems Programming Page 18 of 19
    COMP2017 9017
    • Must use meaningful commits and meaningful comments on commits. 4
    • Other restricted functions may come at a later date.
    • NOT manually use return code 42, reserved by ASAN.
