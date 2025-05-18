# Full Usage Cycle of Collaborative Document Editor

Here's a complete example session showing how the collaborative document editing system works from start to finish:

## 1. Starting the Server

```bash
# Build and start the container
docker compose up

# Terminal output shows:
Server started with PID: 123
```

## 2. Connecting Multiple Clients

### Terminal 2 - Bob (write access):

```bash
docker exec -it collab_server ./client 123 bob
# Output:
Connected as: bob
Role: write
Document version: 0
Document:
```

### Terminal 3 - Eve (read-only access):

```bash
docker exec -it collab_server ./client 123 eve
# Output:
Connected as: eve
Role: read
Document version: 0
Document:
```

### Terminal 4 - Ryan (write access):

```bash
docker exec -it collab_server ./client 123 ryan
# Output:
Connected as: ryan
Role: write
Document version: 0
Document:
```

## 3. Editing Commands - Initial Content Creation

### Terminal 2 (Bob):

```bash
i 0 # Collaborative Markdown Editor
# Output:
Document version: 1
Document: # Collaborative Markdown Editor
```

### Terminal 2 (Bob):

```bash
i 30 \n\nThis is a demonstration of the editor.
# Output:
Document version: 2
Document: # Collaborative Markdown Editor

This is a demonstration of the editor.
```

## 4. Terminal 3 (Eve) - Read-only user sees updates:

```
# Output (automatic update):
Document version: 2
Document: # Collaborative Markdown Editor

This is a demonstration of the editor.
```

## 5. Terminal 4 (Ryan) - Adding formatting:

```bash
i 65 \n\nWe can **format** text in many ways.
# Output:
Document version: 3
Document: # Collaborative Markdown Editor

This is a demonstration of the editor.

We can **format** text in many ways.
```

## 6. Terminal 2 (Bob) - Creating a list:

```bash
i 105 \n\n
# Output:
Document version: 4
Document: # Collaborative Markdown Editor

This is a demonstration of the editor.

We can **format** text in many ways.

```

```bash
i 107 1. First item\n2. Second item\n3. Third item
# Output:
Document version: 5
Document: # Collaborative Markdown Editor

This is a demonstration of the editor.

We can **format** text in many ways.

1. First item
2. Second item
3. Third item
```

## 7. Terminal 4 (Ryan) - Adding a blockquote:

```bash
i 149 \n\n> This is a blockquote example.
# Output:
Document version: 6
Document: # Collaborative Markdown Editor

This is a demonstration of the editor.

We can **format** text in many ways.

1. First item
2. Second item
3. Third item

> This is a blockquote example.
```

## 8. Terminal 2 (Bob) - Modifying text with delete:

```bash
d 21 22
# Output:
Document version: 7
Document: # Collaborative Markdown Editor

This is a demo of the editor.

We can **format** text in many ways.

1. First item
2. Second item
3. Third item

> This is a blockquote example.
```

## 9. Terminal 4 (Ryan) - Adding a second heading:

```bash
i 186 \n\n## Advanced Features
# Output:
Document version: 8
Document: # Collaborative Markdown Editor

This is a demo of the editor.

We can **format** text in many ways.

1. First item
2. Second item
3. Third item

> This is a blockquote example.

## Advanced Features
```

## 10. Terminal 3 (Eve) - Attempting to edit (fails):

```bash
i 205 \nEditing is restricted.
# Output:
Error: Read-only access. Cannot modify document.
```

## 11. Terminal 2 (Bob) - Reverting to earlier version:

```bash
version 5
# Output:
Document version: 5
Document: # Collaborative Markdown Editor

This is a demonstration of the editor.

We can **format** text in many ways.

1. First item
2. Second item
3. Third item
```

## 12. Terminal 4 (Ryan) - Continuing from reverted version:

```bash
i 149 \n\n> A different blockquote after version rollback.
# Output:
Document version: 9
Document: # Collaborative Markdown Editor

This is a demonstration of the editor.

We can **format** text in many ways.

1. First item
2. Second item
3. Third item

> A different blockquote after version rollback.
```

## 13. Shutting Down

### Terminal 2 (Bob):

```bash
q
# Connection closes
```

### Terminal 3 (Eve):

```bash
q
# Connection closes
```

### Terminal 4 (Ryan):

```bash
q
# Connection closes
```

### Terminal 1 (Server):

```
# Press Ctrl+C
Cleaning up processes and files...
# Container stops
```

This complete cycle demonstrates the collaborative nature of the editor, showing how multiple users can interact with the document simultaneously, with proper version control and role-based access restrictions.
