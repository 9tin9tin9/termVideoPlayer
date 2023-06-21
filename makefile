CXX = gcc-13
CXXFLAGS = -g -O3 -fopenmp
# CXXFLAGS += -fsanitize=address
LDFLAGS =
TARGET_DIR = target
SRC_DIR = src
MODULES = main ansipixel cbmp printf imageutil
TARGET = main

# prerequisites for each module
# add the module even if there is no prerequisite
main = ansipixel.h cbmp.h imageutil.h
ansipixel = ansipixel.h printf.h
cbmp = cbmp.h
printf = printf.h
imageutil = imageutil.h

all: $(TARGET_DIR) ./$(TARGET_DIR)/$(TARGET)

run: all
	@./$(TARGET_DIR)/$(TARGET) $(ARGS)

$(TARGET_DIR):
	@if [ ! -e $(TARGET_DIR) ]; then mkdir $(TARGET_DIR); fi

OBJ = $(addprefix $(TARGET_DIR)/, $(addsuffix .o, $(MODULES)))

$(TARGET_DIR)/$(TARGET): $(OBJ)
	@echo linking $@
	@$(CXX) $(LDFLAGS) $(CXXFLAGS) $^ -o $@

.SECONDEXPANSION:

$(TARGET_DIR)/%.o: $(SRC_DIR)/%.c $$(addprefix $(SRC_DIR)/, $$($$*)) makefile
	@echo compiling $@
	@$(CXX) $(CXXFLAGS) $< -c -o $@

clean:
	rm -rf $(TARGET_DIR)

.PHONY: clean
