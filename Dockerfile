# Use a standard GCC image as the base
FROM gcc:latest

# Set the working directory inside the container
WORKDIR /app

# Copy source code, headers, Makefile, roles.txt and entrypoint script
COPY src/ ./src/
COPY include/ ./include/
COPY Makefile .
COPY roles.txt .
COPY entrypoint.sh .

# Make sure the entrypoint script is executable
RUN chmod +x /app/entrypoint.sh

# Entrypoint script will build the executables when container starts
# so that it always builds with the latest mounted source code
ENTRYPOINT ["/app/entrypoint.sh"]

# Default time interval argument (1000ms)
CMD ["1000"]