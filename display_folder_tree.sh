#!/bin/bash

# Fonction récursive pour afficher l'arborescence
function display_tree {
    local dir=$1
    local indent=$2

    # Liste des répertoires et fichiers à exclure
    local exclude_dirs=(".cache" ".vscode" "sdl_lib_src" "SDL" "build" "Documentation")

    # Vérifie si le répertoire doit être exclu
    local exclude=0
    for exclude_dir in "${exclude_dirs[@]}"; do
        if [[ "$dir" == */$exclude_dir ]] || [[ "$dir" == */$exclude_dir/* ]]; then
            exclude=1
            break
        fi
    done

    if [[ $exclude -eq 1 ]]; then
        return
    fi

    # Affiche le répertoire ou fichier actuel
    local item_name="${dir##*/}"
    if [[ -d "$dir" ]]; then
        echo "${indent}└── $item_name/"
    else
        echo "${indent}└── $item_name"
    fi

    # Parcourt les éléments du répertoire
    local new_indent="$indent    "
    for item in "$dir"/*; do
        if [[ -d "$item" ]]; then
            display_tree "$item" "$new_indent"
        elif [[ -f "$item" ]]; then
            echo "${new_indent}├── ${item##*/}"
        fi
    done
}

# Point de départ de l'arborescence
root_dir="."

# Appel de la fonction récursive
display_tree "$root_dir" ""
