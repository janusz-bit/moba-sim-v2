# Sphinx configuration for moba-sim documentation.
# Doxygen C++ API reference is pulled in via Breathe.

import os
import sys

sys.path.insert(0, os.path.abspath(".."))

# -- Project info ------------------------------------------------------------

project = "moba-sim"
author = "moba-sim contributors"
release = "0.1.0"

# -- General config ----------------------------------------------------------

extensions = [
    "breathe",
    "sphinx.ext.autodoc",
    "sphinx.ext.napoleon",
    "sphinx.ext.viewcode",
    "sphinx.ext.intersphinx",
]

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

# -- Breathe (Doxygen bridge) ------------------------------------------------

breathe_projects = {
    "moba-sim": os.path.abspath("../xml"),
}
breathe_default_project = "moba-sim"
breathe_default_members = ("members", "undoc-members")

# -- HTML output -------------------------------------------------------------

html_theme = "furo"
html_static_path = ["_static"]
html_title = "moba-sim Documentation"

# -- Intersphinx -------------------------------------------------------------

intersphinx_mapping = {
    "python": ("https://docs.python.org/3", None),
}