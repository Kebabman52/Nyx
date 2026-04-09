import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'Nyx',
  description: 'Embeddable scripting language by Nemesis Security',
  outDir: './dist',
  head: [['link', { rel: 'icon', href: '/favicon.ico' }]],
  markdown: {
    // Map ```nyx to Rust highlighting — closest syntax match
    languageAlias: {
      'nyx': 'rust'
    }
  },
  themeConfig: {
    logo: '/logo.svg',
    nav: [
      { text: 'Guide', link: '/guide/introduction' },
      { text: 'Embedding', link: '/embedding/getting-started' },
      { text: 'Reference', link: '/reference/stdlib' },
      { text: 'Nemesis', link: 'https://nemesistech.ee' }
    ],
    sidebar: {
      '/guide/': [
        {
          text: 'Getting Started',
          items: [
            { text: 'Introduction', link: '/guide/introduction' },
            { text: 'Installation', link: '/guide/installation' },
            { text: 'Hello World', link: '/guide/hello-world' },
          ]
        },
        {
          text: 'Language Basics',
          items: [
            { text: 'Variables & Types', link: '/guide/variables' },
            { text: 'Functions', link: '/guide/functions' },
            { text: 'Control Flow', link: '/guide/control-flow' },
            { text: 'Classes', link: '/guide/classes' },
          ]
        },
        {
          text: 'Advanced',
          items: [
            { text: 'Collections', link: '/guide/collections' },
            { text: 'Error Handling', link: '/guide/error-handling' },
            { text: 'Pattern Matching', link: '/guide/pattern-matching' },
            { text: 'Coroutines', link: '/guide/coroutines' },
            { text: 'Modules', link: '/guide/modules' },
            { text: 'Packages', link: '/guide/packages' },
          ]
        }
      ],
      '/embedding/': [
        {
          text: 'Embedding Nyx',
          items: [
            { text: 'Getting Started', link: '/embedding/getting-started' },
            { text: 'C API Reference', link: '/embedding/c-api' },
            { text: 'Host Functions', link: '/embedding/host-functions' },
            { text: 'Native Modules', link: '/embedding/native-modules' },
            { text: 'Bytecode Compilation', link: '/embedding/bytecode' },
          ]
        }
      ],
      '/reference/': [
        {
          text: 'Reference',
          items: [
            { text: 'Standard Library', link: '/reference/stdlib' },
            { text: 'String Methods', link: '/reference/string-methods' },
            { text: 'List Methods', link: '/reference/list-methods' },
            { text: 'Map Methods', link: '/reference/map-methods' },
            { text: 'Set Methods', link: '/reference/set-methods' },
          ]
        }
      ]
    },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/nemesis-security/nyx' }
    ],
    footer: {
      message: 'Built by Nemesis Security',
      copyright: 'Nemesis Technologies'
    },
    search: {
      provider: 'local'
    }
  }
})
