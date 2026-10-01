import path from 'node:path'
import { fileURLToPath } from 'node:url'
import svelte from 'eslint-plugin-svelte'
import tsParser from '@typescript-eslint/parser'
import tsPlugin from '@typescript-eslint/eslint-plugin'
import svelteParser from 'svelte-eslint-parser'
import tailwind from 'eslint-plugin-tailwindcss'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

export default [
  ...svelte.configs['flat/recommended'],
  {
    settings: {
      tailwindcss: {
        cssConfigPath: 'src/app.css',
        cssFiles: ['src/app.css'],
        callees: ['cn', 'clsx'],
      },
    },
    files: ['**/*.svelte'],
    plugins: {
      '@typescript-eslint': tsPlugin,
      tailwindcss: tailwind,
    },
    languageOptions: {
      parser: svelteParser,
      parserOptions: {
        parser: tsParser,
        extraFileExtensions: ['.svelte'],
        project: path.resolve(__dirname, './tsconfig.app.json'),
      },
    },
    rules: {
      'svelte/valid-compile': 'error',
      '@typescript-eslint/no-deprecated': 'warn',
      'tailwindcss/enforces-canonical-classname': 'warn',
      'tailwindcss/enforces-shorthand': 'warn',
      'tailwindcss/no-custom-classname': 'off',
    },
  },
  {
    settings: {
      tailwindcss: {
        cssConfigPath: 'src/app.css',
        cssFiles: ['src/app.css'],
        callees: ['cn', 'clsx'],
      },
    },
    files: ['**/*.svelte.ts', 'src/**/*.ts'],
    plugins: {
      '@typescript-eslint': tsPlugin,
      tailwindcss: tailwind,
    },
    languageOptions: {
      parser: tsParser,
      parserOptions: {
        project: path.resolve(__dirname, './tsconfig.app.json'),
      },
    },
    rules: {
      '@typescript-eslint/no-deprecated': 'warn',
      'tailwindcss/enforces-canonical-classname': 'warn',
      'tailwindcss/enforces-shorthand': 'warn',
      'tailwindcss/no-custom-classname': 'off',
    },
  },
  {
    ignores: [
      'node_modules/**',
      'dist/**',
      '.svelte-kit/**',
      'vite.config.ts',
      'eslint.config.js',
      'svelte.config.js',
    ],
  },
]
