# CS375 Lab 8 - shortcuts run from the repo root.
# src/Makefile is the one submitted to Canvas; this file calls it.
SRC    = src
SUBMIT = submission
DATA   = report/data

all:
	$(MAKE) -C $(SRC)

run: all
	cd $(SRC) && ./lab

$(DATA):
	mkdir -p $@

# Reruns the experiments and refreshes the report's tables and graphs
data: all | $(DATA)
	cd $(SRC) && ./lab --experiment --csv ../$(DATA)

# Builds report.pdf, then removes LaTeX's working files
report:
	cd report && latexmk -pdf -interaction=nonstopmode -halt-on-error report.tex
	cd report && latexmk -c report.tex

# Everything that goes on Canvas, in one folder
submission: all report
	rm -rf $(SUBMIT)
	mkdir -p $(SUBMIT)
	cp $(SRC)/main.cpp $(SRC)/generate_addresses.cpp $(SRC)/Makefile $(SRC)/addresses.txt report/report.pdf $(SUBMIT)/
	rm -f $(SRC)/lab $(SRC)/generate
	@echo "Screenshots found: $$(ls report/screenshots/*.png 2>/dev/null | wc -l) of 11"

clean:
	$(MAKE) -C $(SRC) clean
	cd report && latexmk -c report.tex

.PHONY: all run data report submission clean
