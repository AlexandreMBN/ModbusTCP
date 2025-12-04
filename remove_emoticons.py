#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import glob
import sys

# Lista de emoticons para remover
emoticons = [
    '\U0001F4CB', '\U0001F4C4', '\U0001F527', '\u2705', '\u274C', '\u26A0\uFE0F', '\U0001F4C2', '\U0001F4E1', '\U0001F680', '\U0001F4DD',
    '\U0001F50D', '\U0001F504', '\U0001F4CA', '\U0001F7E2', '\U0001F7E1', '\U0001F534', '\U0001F4E2', '\U0001F310', '\U0001F4BE', '\u26A1',
    '\U0001F4E4', '\U0001F4E5', '\U0001F9F9', '\u23F1\uFE0F', '\u23E9', '\u26A0', '\u23F1', '\u23ED\uFE0F', '\u23ED'
]

# Caminho dos arquivos
src_path = r'c:\Users\Alexandre\Desktop\BASE\webserver28\WebServerCompleto_28_nov-A-semEmoticon\src\*.c'
files = glob.glob(src_path)

print("Processando {} arquivos...".format(len(files)), file=sys.stderr)

count = 0
for file_path in files:
    try:
        # Ler o arquivo
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        original_length = len(content)
        
        # Remover cada emoticon
        for emoticon in emoticons:
            content = content.replace(emoticon + ' ', '')
            content = content.replace(emoticon, '')
        
        # Escrever o arquivo apenas se houver mudanças
        if len(content) < original_length:
            with open(file_path, 'w', encoding='utf-8', newline='') as f:
                f.write(content)
            print("OK: {}".format(os.path.basename(file_path)), file=sys.stderr)
            count += 1
        else:
            print("SKIP: {} (sem emoticons)".format(os.path.basename(file_path)), file=sys.stderr)
    except Exception as e:
        print("ERRO: {}: {}".format(os.path.basename(file_path), e), file=sys.stderr)

print("\nConcluido! {} arquivos processados.".format(count), file=sys.stderr)
