import DefaultTheme from 'vitepress/theme'
import CopyForLLM from './CopyForLLM.vue'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('CopyForLLM', CopyForLLM)
  }
}
