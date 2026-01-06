.SILENT:

GREEN		= \033[32m
RED			= \033[31m
BLINK		= \033[5m
RESET		= \033[0m

NAME        = webserv
CC          = c++
CFLAGS      = -Wall -Wextra -Werror -std=c++98 -I include
RM          = rm -rf

# Objects and Directories
OBJS        = $(SRCS:.cpp=.o)
WWW_DIR     = /tmp/webserv/www/html /tmp/webserv/www/defaultPages/error /tmp/webserv/www/html/uploads /tmp/webserv/www/html/cgi-bin

# List all subdirectories containing source files
SRCS_DIRS   = srcs/ srcs/config/ srcs/http/ srcs/network/ srcs/utils/ srcs/cgi/

# Find all .cpp files in the source directories
SRCS        = $(foreach dir, $(SRCS_DIRS), $(wildcard $(dir)*.cpp))


# **************************************************************************** #
# RULES                                                                       #
# **************************************************************************** #

# Main rule - makes the executable
$(NAME): $(OBJS)
	@mkdir -p $(WWW_DIR)
	@$(CC) $(CFLAGS) $(OBJS) -o $(NAME)
	@echo "Directories created in $(WWW_DIR)"
	@echo "$(GREEN)✅ $(NAME) compiled successfully!$(RESET)"

# Rule to compile each .cpp file into a .o file
%.o: %.cpp
	@$(CC) $(CFLAGS) -c $< -o $@

# Standard rules
all: $(NAME)

clean:
	@$(RM) $(OBJS)
	@echo "🧹 Object files cleaned!"

fclean: clean
	@$(RM) $(NAME)
	@$(RM) $(WWW_DIR)
	@echo "🗑️ Executable and $(WWW_DIR) removed!"

re: fclean all

# Prevent conflicts with files of the same name
.PHONY: all clean fclean re
