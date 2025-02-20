import { toString, IllegalConstructor } from '../util.js'
import process from '../process.js'

export const MIN_CHANNEL_SUBSCRIBER_SIZE = 64

/**
 * Normalizes a channel name to lower case replacing white space,
 * hyphens (-), underscores (_), with dots (.).
 * @ignore
 */
export function normalizeName (group, name) {
  if (group && !name) {
    name = group.name || group || ''
  } else if (group && name) {
    const groupName = normalizeName(group.name || group || '')
    if (!name.startsWith(groupName)) {
      name = [groupName, name].filter(Boolean).join('.')
    }
  }

  if (typeof name !== 'string') {
    throw new TypeError(`Expecting 'name' to be a string. Got: ${typeof name}`)
  }

  const normalized = name
    .toLowerCase()
    .replace(/\s+/g, '.')
    .replace(/(-|_)/g, '.')

  return normalized
}

/**
 * TODO(@jwerle): documentation
 * @ignore
 */
export class Channel {
  subscribers = new Array(MIN_CHANNEL_SUBSCRIBER_SIZE)
  #subscribed = 0

  constructor (name) {
    this.name = name
    this.group = null
  }

  get hasSubscribers () {
    return false
  }

  get length () {
    return this.#subscribed || 0
  }

  get [Symbol.iterator] () {
    return this.subscribers
  }

  channel (name) {
    name = normalizeName(this, name)
    return registry.channel(name)
  }

  /**
   * Adds an `onMessage` subscription callback to the channel.
   * @return {boolean}
   */
  subscribe (onMessage) {
    if (typeof onMessage !== 'function') {
      throw new TypeError(
        `Expecting 'onMessage' to be a function. Got: ${typeof onMessage}`
      )
    }

    if (this.#subscribed >= this.subscribers.length) {
      this.subscribers.push(onMessage)
      this.#subscribed = this.subscribers.length
    } else {
      const i = this.#subscribed
      this.subscribers[i] = onMessage
      this.#subscribed++
    }

    Object.setPrototypeOf(this, ActiveChannel.prototype)

    return true
  }

  /**
   * Removes an `onMessage` subscription callback from the channel.
   * @param {function} onMessage
   * @return {boolean}
   */
  unsubscribe (onMessage) {
    if (typeof onMessage !== 'function') {
      throw new TypeError(
        `Expecting 'onMessage' to be a function. Got: ${typeof onMessage}`
      )
    }

    const index = this.subscribers.indexOf(onMessage)

    if (index === -1) {
      return false
    }

    if (index >= MIN_CHANNEL_SUBSCRIBER_SIZE) {
      this.subscribers.splice(index, 1)
    } else {
      this.subscribers[index] = undefined
    }

    this.#subscribed--

    return true
  }

  /**
   * A no-op for `Channel` instances. This function always returns `false`.
   * @return {boolean}
   */
  async publish () {
    return false
  }

  /**
   * GC finalizer callback
   * @ignore
   */
  [Symbol.for('gc.finalizer')] (options) {
    return {
      args: [this.name, this.subscribers],
      handle (name, subscribers) {
        if (registry.has(name)) {
          registry.remove(name)
        }

        subscribers.splice(0, subscribers.length)
      }
    }
  }

  /**
   * @ignore
   */
  [Symbol.toStringTag] () {
    return 'DiagnosticChannel'
  }

  /**
   * @ignore
   */
  toString () {
    return toString(this)
  }
}

/**
 * TODO(@jwerle): documentation
 * @ignore
 */
export class ActiveChannel extends Channel {
  get hasSubscribers () {
    return true
  }

  unsubscribe (onMessage) {
    if (!super.unsubscribe(onMessage)) {
      return false
    }

    if (this.length === 0) {
      Object.setPrototypeOf(this, Channel.prototype)
    }
  }

  async publish (message) {
    if (!this.hasSubscribers) return false

    let published = false

    for (const onMessage of this.subscribers) {
      if (typeof onMessage === 'function') {
        try {
          await onMessage(message, this.name, this)
          published = true
        } catch (err) {
          process.nextTick(() => { throw err })
          return false
        }
      }
    }

    return published
  }
}

/**
 * TODO(@jwerle): documentation
 * @ignore
 */
export class ChannelGroup extends Channel {
  /**
   * @param {Array<Channel>} channels
   * @param {string} name
   */
  constructor (name, channels) {
    super(name)

    this.channels = Array.isArray(channels) ? channels : []

    for (const channel of this.channels) {
      channel.group = this
    }
  }

  /**
   * Computed subscribers for all channels in this group.
   */
  get subscribers () {
    return this.channels
      .map((channel) => channel.subscribers)
      .reduce((a, b) => a.concat(b), [])
  }

  /**
   * Number of channels in this group
   */
  get length () {
    return this.channels.length
  }

  /**
   * `true` if any channel in this group has a subscriber, otherwise `false`.
   */
  get hasSubscribers () {
    return this.channels.some((channel) => channel.hasSubscribers)
  }

  /**
   * @ignore
   */
  get [Symbol.iterator] () {
    return this.channels
  }

  /**
   * TODO
   * @ignore
   */
  subscribe (name, onMessage) {
    if (typeof name === 'function') {
      onMessage = name
      name = null
    }

    const selection = name ? this.select(name) : this.select('*')

    for (const { channel } of selection) {
      if (!channel.subscribe(onMessage)) {
        return false
      }
    }

    return selection.length > 0
  }

  /**
   * TODO
   * @ignore
   */
  unsubscribe (name, onMessage) {
    const selection = this.select(name)

    for (const { channel } of selection) {
      if (!channel.unsubscribe(onMessage)) {
        return false
      }
    }

    return selection.length > 0
  }

  /**
   * Gets or creates a channel for this group.
   * @param {string} name
   * @return {Channel}
   */
  channel (name) {
    name = normalizeName(this, name)
    const selection = this.select([name])

    if (!selection.length) {
      const channel = registry.channel(name)
      channel.group = this
      this.channels.push(channel)
      return channel
    }

    if (selection.length > 1) {
      throw new RangeError(
        `Expecting 1 selection in 'group.channel()' for name '${name}'`
      )
    }

    return selection[0].channel
  }

  /**
   * TODO
   * @ignore
   */
  select (keys, hasSubscribers = false) {
    const selection = []

    if (!keys || !keys.length) {
      return selection
    }

    if (typeof keys === 'string') {
      keys = [keys]
    }

    for (const key of keys) {
      const name = normalizeName(this, key)
      const filtered = this.channels.filter(filter(name))
      for (const channel of filtered) {
        selection.push({ name, channel })
      }
    }

    return selection

    function filter (name) {
      const regexSafeName = name.replace(/:/g, '\\:').replace(/\*/g, '.*')
      const regex = new RegExp(`^${regexSafeName}$`, 'g')
      return (channel) => {
        regex.lastIndex = 0 // `RegExp` instances are stateful

        if (channel.name === name || regex.test(channel.name)) {
          return !hasSubscribers || channel.hasSubscribers
        }

        return false
      }
    }
  }

  /**
   * Publish a message to named subscribers in this group where `targets` is an
   * object mapping channel names to messages.
   * @param {string} name
   * @param {object} message
   * @return {boolean}
   */
  async publish (name, message) {
    const pending = []
    const targets = name && message ? { [name]: message } : name
    const entries = Object.entries(targets).map((e) => normalizeEntry(this, e))
    const messages = Object.fromEntries(entries)
    const selection = this.select(Object.keys(messages), true)

    for (const { name, channel } of selection) {
      const message = messages[name]
      if (message && channel.hasSubscribers) {
        pending.push(channel.publish(message))
      }
    }

    const results = await Promise.all(pending)
    return results.length > 0 && results.every((result) => result === true)

    function normalizeEntry (group, entry) {
      return [normalizeName(group, entry[0]), entry[1]]
    }
  }
}

Object.freeze(Channel.prototype)
Object.freeze(ChannelGroup.prototype)
Object.freeze(ActiveChannel.prototype)

/**
 * An object mapping of named channels to `WeakRef<Channel>` instances.
 */
// eslint-disable-next-line new-parens
export const registry = new class ChannelRegistry {
  /**
   * Subscribes callback `onMessage` to channel of `name`.
   * @param {string} name
   * @param {function} onMessage
   * @return {boolean}
   */
  subscribe (name, onMessage) {
    return this.channel(name)?.subscribe(onMessage) ?? false
  }

  /**
   * Unsubscribes callback `onMessage` from channel of `name`.
   * @param {string} name
   * @param {function} onMessage
   * @return {boolean}
   */
  unsubscribe (name, onMessage) {
    return this.channel(name)?.unsubscribe(onMessage) ?? false
  }

  /**
   * Predicate to determine if a named channel has subscribers.
   * @param {string} name
   */
  hasSubscribers (name) {
    return this.get(name)?.hasSubscribers ?? false
  }

  /**
   * Get or set a channel by `name`.
   * @param {string} name
   * @return {Channel}
   */
  channel (name) {
    return this.has(name)
      ? this.get(name)
      : this.set(name, new Channel(name))
  }

  /**
   * Creates a `ChannelGroup` for a set of channels
   * @param {string} name
   * @param {Array<string>} [channels]
   * @return {ChannelGroup}
   */
  group (name, channels) {
    if (channels && !Array.isArray(channels)) {
      channels = [channels]
    }

    channels = (channels || [])
      .map((channel) => channel instanceof Channel
        ? channel
        : this.channel(normalizeName(name, channel))
      )

    return new ChannelGroup(name, channels)
  }

  /**
   * Get a channel by name. The name is normalized.
   * @param {string} name
   * @return {Channel?}
   */
  get (name) {
    return this[normalizeName(name)]?.deref?.() ?? null
  }

  /**
   * Checks if a channel is known by  name. The name is normalized.
   * @param {string} name
   * @return {boolean}
   */
  has (name) {
    return normalizeName(name) in this
  }

  /**
   * Set a channel by name. The name is normalized.
   * @param {string} name
   * @param {Channel} channel
   * @return {Channel?}
   */
  set (name, channel) {
    if (channel instanceof Channel === false) {
      const tag = String(channel?.[Symbol.toStringTag]?.() ?? channel)
      throw new TypeError(
        `Expecting 'channel' to be an instance of 'Channel'. Got ${tag}`
      )
    }

    this[normalizeName(name)] = new WeakRef(channel)

    return channel
  }

  /**
   * Removes a channel by `name`
   * @return {boolean}
   */
  remove (name) {
    name = normalizeName(name)
    return name in this && delete this[name]
  }

  /**
   * @ignore
   */
  [Symbol.toStringTag] () {
    return 'DiagnosticChannels'
  }

  /**
   * @ignore
   */
  toString () {
    return toString(this)
  }

  /**
   * @ignore
   */
  toJSON () {
    const json = {}

    for (const name in this) {
      const channel = this.get(name)
      if (channel) {
        json[name] = channel.toJSON()
      }
    }

    return json
  }
}

// make construction illegal
Object.assign(Object.getPrototypeOf(registry), {
  constructor: IllegalConstructor
})

export default registry
